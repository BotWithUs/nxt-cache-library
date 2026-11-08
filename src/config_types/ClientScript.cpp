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
    {OperandKind::None, "none"},
    {OperandKind::Byte, "byte"},
    {OperandKind::Int, "int"},
    {OperandKind::Long, "long"},
    {OperandKind::String, "string"},
};

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

std::string_view decodeFailureName(DecodeFailure failure)
{
    switch (failure)
    {
        case DecodeFailure::None:           return "none";
        case DecodeFailure::Truncated:      return "truncated";
        case DecodeFailure::BadHeader:      return "bad_header";
        case DecodeFailure::UnknownOpcode:  return "unknown_opcode";
        case DecodeFailure::NoOpcodeTable:  return "no_opcode_table";
        case DecodeFailure::BadOperandKind: return "bad_operand_kind";
    }
    return "?";
}

std::optional<OpcodeTable> parseOpcodeTable(std::string_view json, std::string *outError)
{
    // Provisional schema until the RE peer's opcodes.json lands:
    // { "revision": "...", "opcodes": [ { "opcode": n, "operand": "int", "name": "..." } ] }
    try
    {
        auto doc = nlohmann::json::parse(json);
        OpcodeTable table;
        if (doc.contains("revision") && doc["revision"].is_string())
        {
            table.revision = doc["revision"].get<std::string>();
        }
        for (const auto &entry : doc.at("opcodes"))
        {
            OpcodeSpec spec;
            auto kind = parseOperandKind(entry.at("operand").get<std::string>());
            if (!kind)
            {
                if (outError)
                {
                    *outError = "unknown operand kind " + entry.at("operand").dump();
                }
                return std::nullopt;
            }
            spec.operand = *kind;
            if (entry.contains("name") && entry["name"].is_string())
            {
                spec.name = entry["name"].get<std::string>();
            }
            table.ops[entry.at("opcode").get<int>()] = std::move(spec);
        }
        return table;
    }
    catch (const std::exception &e)
    {
        if (outError)
        {
            *outError = e.what();
        }
        return std::nullopt;
    }
}

ClientScript decodeClientScript(int id, const uint8_t *data, size_t size, const OpcodeTable *table)
{
    (void)data;
    (void)size;
    (void)table;
    ClientScript script;
    script.id = id;
    script.failure = DecodeFailure::BadHeader;
    script.error = "decoder not implemented yet";
    return script;
}

const OpcodeSpec *OpcodeTable::find(int opcode) const
{
    auto it = ops.find(opcode);
    return it == ops.end() ? nullptr : &it->second;
}

std::string toAssembly(const ClientScript &script, const OpcodeTable *table)
{
    std::string out;
    char line[96];
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

        switch (insn.operand)
        {
            case OperandKind::None:
                break;
            case OperandKind::Byte:
            case OperandKind::Int:
            case OperandKind::Long:
                std::snprintf(line, sizeof(line), "%" PRId64, insn.intOperand);
                out += line;
                break;
            case OperandKind::String:
                out += nlohmann::json(insn.stringOperand)
                           .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
                break;
        }
        while (!out.empty() && out.back() == ' ')
        {
            out.pop_back();
        }
        out += '\n';
    }
    return out;
}

}  // namespace cs2
