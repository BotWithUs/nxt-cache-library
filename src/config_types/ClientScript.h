#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ClientScript (CS2): one compiled client script. JS5 index 12, one group per
// script id, the script in file 0.
//
// The decoder knows the container layout and the operand encodings, but never
// what an opcode means: opcode numbers are reshuffled per client build, so the
// operand kind of each opcode (and, optionally, its name) comes in from the
// caller as an OpcodeTable loaded from a per-revision opcodes.json.
namespace cs2 {

constexpr int kClientScriptIndex = 12;

// How the bytes after an opcode are read.
enum class OperandKind
{
    None,     // no operand bytes
    Byte,     // u8
    Int,      // i32, big-endian
    Long,     // i64, big-endian
    String,   // NUL-terminated cp1252, returned as UTF-8
};

[[nodiscard]] std::string_view operandKindName(OperandKind kind);
[[nodiscard]] std::optional<OperandKind> parseOperandKind(std::string_view name);

struct OpcodeSpec
{
    OperandKind operand{OperandKind::None};
    std::string name;  // empty when the table carries no name
};

// Per-revision opcode table. An opcode missing from the table is "unknown":
// its operand width is unknown, so decoding that script stops there.
struct OpcodeTable
{
    std::string revision;  // free text from the table file, may be empty
    std::map<int, OpcodeSpec> ops;

    [[nodiscard]] const OpcodeSpec *find(int opcode) const;
};

// Parse an opcodes.json document. Returns nullopt and sets *outError on a
// malformed document; never throws.
[[nodiscard]] std::optional<OpcodeTable> parseOpcodeTable(std::string_view json,
                                                          std::string *outError);

struct Instruction
{
    uint32_t index{0};       // instruction number (the unit jump targets count in)
    uint32_t byteOffset{0};  // offset of the opcode within the script file
    int opcode{0};
    OperandKind operand{OperandKind::None};
    int64_t intOperand{0};   // Byte / Int / Long
    std::string stringOperand;
};

struct SwitchCase
{
    int32_t value{0};
    int32_t target{0};  // jump offset, as stored
};

struct SwitchTable
{
    std::vector<SwitchCase> cases;
};

// Where a decode stopped short.
enum class DecodeFailure
{
    None,
    Truncated,        // the file ends inside the header, a table, or an operand
    BadHeader,        // header counts are impossible for the file size
    UnknownOpcode,    // opcode not in the table: operand width unknown
    NoOpcodeTable,    // no table given, so no operand width is known
    BadOperandKind,   // the table names a kind this encoding cannot carry
};

[[nodiscard]] std::string_view decodeFailureName(DecodeFailure failure);

struct ClientScript
{
    int id{-1};
    std::optional<std::string> name;

    int intArgCount{0};
    int stringArgCount{0};
    int longArgCount{0};
    int intLocalCount{0};
    int stringLocalCount{0};
    int longLocalCount{0};
    int declaredInstructionCount{0};

    std::vector<Instruction> instructions;
    std::vector<SwitchTable> switches;

    DecodeFailure failure{DecodeFailure::None};
    std::string error;  // human-readable detail when failure != None

    [[nodiscard]] bool isComplete() const { return failure == DecodeFailure::None; }
};

// Decode one script file. Never throws on malformed data: a decode that stops
// early keeps everything read so far and records why in failure/error.
// table may be null; then only the header and switch tables are decoded.
[[nodiscard]] ClientScript decodeClientScript(int id, const uint8_t *data, size_t size,
                                              const OpcodeTable *table);

// Each instruction on one line: "index  opcode  [name]  operand". Names come
// only from table; with no table (or no name) the opcode is printed raw.
[[nodiscard]] std::string toAssembly(const ClientScript &script, const OpcodeTable *table);

}  // namespace cs2
