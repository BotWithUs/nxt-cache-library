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

// How the bytes after an opcode are read (format: the RE notes for 951-3).
enum class OperandKind
{
    Byte,           // u8, zero-extended
    Int,            // i32
    Var,            // u8 domain, u16 var id, u8 flag
    Varbit,         // u24 varbit id, u8 flag (951 encoding; u16 id before 950)
    TypedConstant,  // u8 type, then 0: i32, 1: i64, 2: string, 3+: nothing
};

// The value a TypedConstant carries, picked by its type byte.
enum class ConstantType
{
    Int,
    Long,
    String,
    Other,  // type byte 3+: no value follows; the byte is kept in intOperand
};

[[nodiscard]] std::string_view operandKindName(OperandKind kind);
[[nodiscard]] std::optional<OperandKind> parseOperandKind(std::string_view name);
[[nodiscard]] std::string_view constantTypeName(ConstantType type);

struct OpcodeSpec
{
    OperandKind operand{OperandKind::Byte};
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

// Parse an opcodes.json document: a JSON array of objects, each with at least
// "opcode" (int) and "operand" ("byte" | "int" | "var" | "varbit" |
// "typed_constant"), and optionally "name". Other fields are ignored. Returns
// nullopt and sets *outError on a malformed document; never throws.
[[nodiscard]] std::optional<OpcodeTable> parseOpcodeTable(std::string_view json,
                                                          std::string *outError);

struct Instruction
{
    uint32_t index{0};       // instruction number within the script
    uint32_t byteOffset{0};  // offset of the opcode within the script file
    int opcode{0};
    bool isKnown{true};      // false: not in the table, so no operand was read
    OperandKind operand{OperandKind::Byte};
    ConstantType constant{ConstantType::Int};  // TypedConstant only
    int64_t intOperand{0};   // Byte / Int value; Var / Varbit flag; constant
                             // int or long; Other: the type byte
    int varDomain{0};        // Var only
    int varId{0};            // Var / Varbit id
    std::string stringOperand;  // String constant
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
    Truncated,        // an operand or the name runs past the instruction stream
    BadFooter,        // footer or switch region inconsistent with the file size
    UnknownOpcode,    // opcode not in the table: operand width unknown
    NoOpcodeTable,    // no table given, so no operand width is known
    CountMismatch,    // stream decoded, but not the footer's instruction count
};

[[nodiscard]] std::string_view decodeFailureName(DecodeFailure failure);

struct ClientScript
{
    int id{-1};
    std::optional<std::string> name;

    // Footer counts. Locals include the args (args are the first locals).
    int intArgCount{0};
    int stringArgCount{0};
    int longArgCount{0};
    int intLocalCount{0};
    int stringLocalCount{0};
    int longLocalCount{0};
    uint32_t declaredInstructionCount{0};

    std::vector<Instruction> instructions;
    std::vector<SwitchTable> switches;

    DecodeFailure failure{DecodeFailure::None};
    std::string error;  // human-readable detail when failure != None

    [[nodiscard]] bool isComplete() const { return failure == DecodeFailure::None; }
};

// Decode one script file. Never throws on malformed data: a decode that stops
// early keeps everything read so far and records why in failure/error.
// table may be null; then only the footer, switch tables and name decode.
//
// Layout, footer first (all big-endian), S = u16 at [len-2], F = len - S - 18:
//   [0..]   name, NUL-terminated cp1252 (empty -> no name)
//   [..F)   instruction stream: u16 opcode + operand per OperandKind
//   [F]     u32 instruction count; u16 int/string/long locals; u16
//           int/string/long args; u8 switch count N; N x { u16 cases;
//           cases x { i32 key; i32 target } }
//   [len-2] u16 S = byte length of the switch count byte + tables
[[nodiscard]] ClientScript decodeClientScript(int id, const uint8_t *data, size_t size,
                                              const OpcodeTable *table);

// Each instruction on one line: "index  opcode  [name]  operand". Names come
// only from table; with no table (or no name) the opcode is printed raw. An
// instruction whose opcode is not in the table prints as "<opcode>  ??".
[[nodiscard]] std::string toAssembly(const ClientScript &script, const OpcodeTable *table);

}  // namespace cs2
