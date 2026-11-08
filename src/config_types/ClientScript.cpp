#include "config_types/ClientScript.h"

#include <nlohmann/json.hpp>

#include <cinttypes>
#include <cstdio>
#include <exception>

namespace cs2 {

namespace {

constexpr struct
{
    OperandKind kind;
    std::string_view name;
} kOperandKindNames[] = {
    {OperandKind::Byte, "byte"},
    {OperandKind::Int, "int"},
    {OperandKind::Var, "var"},
    {OperandKind::Varbit, "varbit"},
    {OperandKind::TypedConstant, "typed_constant"},
};

// u32 count + 6 x u16 locals/args, then the switch region, then the u16 S.
constexpr size_t kFooterFixedBytes = 16;
constexpr size_t kFooterTrailerBytes = 2;

// Bounds-checked big-endian cursor over [begin, end). Every read reports
// failure instead of returning a sentinel, so malformed data can never be
// mistaken for a value or read past the buffer.
class Reader
{
    const uint8_t *begin_;
    const uint8_t *cur_;
    const uint8_t *end_;
public:
    Reader(const uint8_t *begin, const uint8_t *end) : begin_(begin), cur_(begin), end_(end) {}

    [[nodiscard]] size_t offset() const { return static_cast<size_t>(cur_ - begin_); }
    [[nodiscard]] size_t remaining() const { return static_cast<size_t>(end_ - cur_); }

    void skip(size_t count)
    {
        cur_ += count < remaining() ? count : remaining();
    }

    bool readU8(uint32_t &outValue)
    {
        if (remaining() < 1)
        {
            return false;
        }
        outValue = *cur_++;
        return true;
    }

    bool readU16(uint32_t &outValue)
    {
        if (remaining() < 2)
        {
            return false;
        }
        outValue = (static_cast<uint32_t>(cur_[0]) << 8) | cur_[1];
        cur_ += 2;
        return true;
    }

    bool readI32(int32_t &outValue)
    {
        if (remaining() < 4)
        {
            return false;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; i++)
        {
            v = (v << 8) | *cur_++;
        }
        outValue = static_cast<int32_t>(v);
        return true;
    }

    bool readU24(uint32_t &outValue)
    {
        if (remaining() < 3)
        {
            return false;
        }
        outValue = (static_cast<uint32_t>(cur_[0]) << 16) | (static_cast<uint32_t>(cur_[1]) << 8) | cur_[2];
        cur_ += 3;
        return true;
    }

    bool readU32(uint32_t &outValue)
    {
        int32_t v = 0;
        if (!readI32(v))
        {
            return false;
        }
        outValue = static_cast<uint32_t>(v);
        return true;
    }

    bool readI64(int64_t &outValue)
    {
        if (remaining() < 8)
        {
            return false;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
        {
            v = (v << 8) | *cur_++;
        }
        outValue = static_cast<int64_t>(v);
        return true;
    }

    // NUL-terminated cp1252 -> UTF-8. Fails if no terminator before the end.
    bool readString(std::string &outValue);
};

bool Reader::readString(std::string &outValue)
{
    static constexpr uint16_t kCp1252High[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
        0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    };
    const uint8_t *nul = cur_;
    while (nul < end_ && *nul != 0)
    {
        nul++;
    }
    if (nul == end_)
    {
        return false;
    }
    outValue.clear();
    for (; cur_ < nul; cur_++)
    {
        uint32_t cp = *cur_;
        if (cp >= 0x80 && cp < 0xA0)
        {
            cp = kCp1252High[cp - 0x80];
        }
        if (cp < 0x80)
        {
            outValue += static_cast<char>(cp);
        }
        else if (cp < 0x800)
        {
            outValue += static_cast<char>(0xC0 | (cp >> 6));
            outValue += static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            outValue += static_cast<char>(0xE0 | (cp >> 12));
            outValue += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            outValue += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    cur_ = nul + 1;
    return true;
}

std::string failureText(const char *what, size_t offset)
{
    return std::string(what) + " at byte " + std::to_string(offset);
}

// Footer, switch tables. Returns the stream end F, or nullopt after recording
// BadFooter in script.
std::optional<size_t> decodeFooter(const uint8_t *data, size_t size, ClientScript &script)
{
    if (size < kFooterFixedBytes + 1 + kFooterTrailerBytes)
    {
        script.failure = DecodeFailure::BadFooter;
        script.error = "file of " + std::to_string(size) + " bytes is shorter than a footer";
        return std::nullopt;
    }
    const size_t switchBytes = (static_cast<size_t>(data[size - 2]) << 8) | data[size - 1];
    if (switchBytes < 1 || switchBytes + kFooterFixedBytes + kFooterTrailerBytes > size)
    {
        script.failure = DecodeFailure::BadFooter;
        script.error = "switch region length " + std::to_string(switchBytes) + " does not fit a " +
                       std::to_string(size) + "-byte file";
        return std::nullopt;
    }
    const size_t streamEnd = size - switchBytes - kFooterFixedBytes - kFooterTrailerBytes;

    Reader footer(data + streamEnd, data + size - kFooterTrailerBytes);
    uint32_t counts[7]{};
    footer.readU32(counts[0]);
    for (int i = 1; i < 7; i++)
    {
        footer.readU16(counts[i]);
    }
    script.declaredInstructionCount = counts[0];
    script.intLocalCount = static_cast<int>(counts[1]);
    script.stringLocalCount = static_cast<int>(counts[2]);
    script.longLocalCount = static_cast<int>(counts[3]);
    script.intArgCount = static_cast<int>(counts[4]);
    script.stringArgCount = static_cast<int>(counts[5]);
    script.longArgCount = static_cast<int>(counts[6]);

    uint32_t tableCount = 0;
    footer.readU8(tableCount);
    for (uint32_t t = 0; t < tableCount; t++)
    {
        uint32_t caseCount = 0;
        SwitchTable table;
        bool isOk = footer.readU16(caseCount);
        for (uint32_t c = 0; isOk && c < caseCount; c++)
        {
            SwitchCase sc;
            isOk = footer.readI32(sc.value) && footer.readI32(sc.target);
            table.cases.push_back(sc);
        }
        if (!isOk)
        {
            script.failure = DecodeFailure::BadFooter;
            script.error = "switch table " + std::to_string(t) + " runs past the switch region";
            return std::nullopt;
        }
        script.switches.push_back(std::move(table));
    }
    if (footer.remaining() != 0)
    {
        script.failure = DecodeFailure::BadFooter;
        script.error = std::to_string(footer.remaining()) + " bytes left over after the switch tables";
        return std::nullopt;
    }
    return streamEnd;
}

// u8 type, then 0: i32, 1: i64, 2: string, 3+: nothing.
bool decodeTypedConstant(Reader &stream, Instruction &insn)
{
    uint32_t type = 0;
    if (!stream.readU8(type))
    {
        return false;
    }
    int32_t value = 0;
    switch (type)
    {
        case 0:
            insn.constant = ConstantType::Int;
            if (!stream.readI32(value))
            {
                return false;
            }
            insn.intOperand = value;
            return true;
        case 1:
            insn.constant = ConstantType::Long;
            return stream.readI64(insn.intOperand);
        case 2:
            insn.constant = ConstantType::String;
            return stream.readString(insn.stringOperand);
        default:
            insn.constant = ConstantType::Other;
            insn.intOperand = type;
            return true;
    }
}

// The operand after insn.opcode, per its table kind. False if it runs past
// the stream end.
bool decodeOperand(Reader &stream, Instruction &insn)
{
    uint32_t u = 0;
    int32_t i = 0;
    switch (insn.operand)
    {
        case OperandKind::Byte:
            if (!stream.readU8(u))
            {
                return false;
            }
            insn.intOperand = u;
            return true;
        case OperandKind::Int:
            if (!stream.readI32(i))
            {
                return false;
            }
            insn.intOperand = i;
            return true;
        case OperandKind::Var:
        {
            uint32_t domain = 0;
            uint32_t id = 0;
            if (!stream.readU8(domain) || !stream.readU16(id) || !stream.readU8(u))
            {
                return false;
            }
            insn.varDomain = static_cast<int>(domain);
            insn.varId = static_cast<int>(id);
            insn.intOperand = u;
            return true;
        }
        case OperandKind::Varbit:
        {
            uint32_t id = 0;
            if (!stream.readU24(id) || !stream.readU8(u))
            {
                return false;
            }
            insn.varId = static_cast<int>(id);
            insn.intOperand = u;
            return true;
        }
        case OperandKind::TypedConstant:
            return decodeTypedConstant(stream, insn);
    }
    return false;
}

void decodeInstructions(const uint8_t *data, size_t nameEnd, size_t streamEnd,
                        const OpcodeTable &table, ClientScript &script)
{
    Reader stream(data, data + streamEnd);
    stream.skip(nameEnd);
    while (stream.remaining() != 0)
    {
        Instruction insn;
        insn.index = static_cast<uint32_t>(script.instructions.size());
        insn.byteOffset = static_cast<uint32_t>(stream.offset());
        uint32_t opcode = 0;
        if (!stream.readU16(opcode))
        {
            script.failure = DecodeFailure::Truncated;
            script.error = failureText("opcode cut off by the footer", insn.byteOffset);
            return;
        }
        insn.opcode = static_cast<int>(opcode);
        const OpcodeSpec *spec = table.find(insn.opcode);
        if (!spec)
        {
            insn.isKnown = false;
            script.instructions.push_back(std::move(insn));
            script.failure = DecodeFailure::UnknownOpcode;
            script.error = failureText(("opcode " + std::to_string(opcode) +
                                        " is not in the opcode table, operand width unknown").c_str(),
                                       script.instructions.back().byteOffset);
            return;
        }
        insn.operand = spec->operand;
        if (!decodeOperand(stream, insn))
        {
            script.failure = DecodeFailure::Truncated;
            script.error = failureText(("operand of opcode " + std::to_string(opcode) +
                                        " runs past the instruction stream").c_str(),
                                       insn.byteOffset);
            return;
        }
        script.instructions.push_back(std::move(insn));
    }
    if (script.instructions.size() != script.declaredInstructionCount)
    {
        script.failure = DecodeFailure::CountMismatch;
        script.error = "decoded " + std::to_string(script.instructions.size()) +
                       " instructions, the footer declares " +
                       std::to_string(script.declaredInstructionCount);
    }
}

}  // namespace

std::string_view operandKindName(OperandKind kind)
{
    for (const auto &entry : kOperandKindNames)
    {
        if (entry.kind == kind)
        {
            return entry.name;
        }
    }
    return "?";
}

std::optional<OperandKind> parseOperandKind(std::string_view name)
{
    for (const auto &entry : kOperandKindNames)
    {
        if (entry.name == name)
        {
            return entry.kind;
        }
    }
    return std::nullopt;
}

std::string_view constantTypeName(ConstantType type)
{
    switch (type)
    {
        case ConstantType::Int:    return "int";
        case ConstantType::Long:   return "long";
        case ConstantType::String: return "string";
        case ConstantType::Other:  return "other";
    }
    return "?";
}

std::string_view decodeFailureName(DecodeFailure failure)
{
    switch (failure)
    {
        case DecodeFailure::None:          return "none";
        case DecodeFailure::Truncated:     return "truncated";
        case DecodeFailure::BadFooter:     return "bad_footer";
        case DecodeFailure::UnknownOpcode: return "unknown_opcode";
        case DecodeFailure::NoOpcodeTable: return "no_opcode_table";
        case DecodeFailure::CountMismatch: return "count_mismatch";
    }
    return "?";
}

std::optional<OpcodeTable> parseOpcodeTable(std::string_view json, std::string *outError)
{
    auto fail = [outError](const std::string &message) -> std::optional<OpcodeTable> {
        if (outError)
        {
            *outError = message;
        }
        return std::nullopt;
    };
    try
    {
        const auto doc = nlohmann::json::parse(json);
        if (!doc.is_array())
        {
            return fail("expected a JSON array of opcode objects");
        }
        OpcodeTable table;
        for (const auto &entry : doc)
        {
            const int opcode = entry.at("opcode").get<int>();
            const std::string operand = entry.at("operand").get<std::string>();
            const auto kind = parseOperandKind(operand);
            if (!kind)
            {
                return fail("opcode " + std::to_string(opcode) + ": unknown operand kind \"" + operand + "\"");
            }
            if (opcode < 0 || opcode > 0xFFFF)
            {
                return fail("opcode " + std::to_string(opcode) + " is not a u16");
            }
            OpcodeSpec spec;
            spec.operand = *kind;
            if (entry.contains("name") && entry["name"].is_string())
            {
                spec.name = entry["name"].get<std::string>();
            }
            if (!table.ops.emplace(opcode, std::move(spec)).second)
            {
                return fail("opcode " + std::to_string(opcode) + " is listed twice");
            }
        }
        return table;
    }
    catch (const std::exception &e)
    {
        return fail(e.what());
    }
}

const OpcodeSpec *OpcodeTable::find(int opcode) const
{
    auto it = ops.find(opcode);
    return it == ops.end() ? nullptr : &it->second;
}

ClientScript decodeClientScript(int id, const uint8_t *data, size_t size, const OpcodeTable *table)
{
    ClientScript script;
    script.id = id;
    if (!data)
    {
        size = 0;
    }
    const std::optional<size_t> streamEnd = decodeFooter(data, size, script);
    if (!streamEnd)
    {
        return script;
    }

    Reader nameReader(data, data + *streamEnd);
    std::string name;
    if (!nameReader.readString(name))
    {
        script.failure = DecodeFailure::Truncated;
        script.error = "script name has no terminator before the footer";
        return script;
    }
    if (!name.empty())
    {
        script.name = std::move(name);
    }

    if (!table)
    {
        script.failure = DecodeFailure::NoOpcodeTable;
        script.error = "no opcode table: operand widths unknown, instructions not decoded";
        return script;
    }
    decodeInstructions(data, nameReader.offset(), *streamEnd, *table, script);
    return script;
}

std::string toAssembly(const ClientScript &script, const OpcodeTable *table)
{
    std::string out;
    char line[128];
    for (const Instruction &insn : script.instructions)
    {
        std::snprintf(line, sizeof(line), "%5u  %5d  ", insn.index, insn.opcode);
        out += line;

        const OpcodeSpec *spec = table ? table->find(insn.opcode) : nullptr;
        if (spec && !spec->name.empty())
        {
            out += spec->name;
            out += "  ";
        }

        if (!insn.isKnown)
        {
            out += "??";
        }
        else if (insn.operand == OperandKind::Var)
        {
            std::snprintf(line, sizeof(line), "var %d:%d flag %" PRId64, insn.varDomain, insn.varId,
                          insn.intOperand);
            out += line;
        }
        else if (insn.operand == OperandKind::Varbit)
        {
            std::snprintf(line, sizeof(line), "varbit %d flag %" PRId64, insn.varId, insn.intOperand);
            out += line;
        }
        else if (insn.operand == OperandKind::TypedConstant && insn.constant == ConstantType::String)
        {
            out += nlohmann::json(insn.stringOperand)
                       .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }
        else if (insn.operand == OperandKind::TypedConstant)
        {
            std::snprintf(line, sizeof(line), "%s %" PRId64,
                          std::string(constantTypeName(insn.constant)).c_str(), insn.intOperand);
            out += line;
        }
        else
        {
            std::snprintf(line, sizeof(line), "%" PRId64, insn.intOperand);
            out += line;
        }
        out += '\n';
    }
    return out;
}

}  // namespace cs2
