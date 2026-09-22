#pragma once

#include "config_types/ScriptVarType.h"
#include "core/RSBuffer.h"

#include <cstddef>
#include <cstdint>
#include <string>

// Which branch of the client's default rule applies to a varp.
enum class VarpDefaultRule
{
    None,    // type unknown: no default can be computed
    Type,    // the ScriptVarType's default
    Domain,  // the player domain's default, -1
};

struct VarpDefault
{
    VarpDefaultRule rule{VarpDefaultRule::None};
    bool hasBaseType{false};
    BaseVarType baseType{BaseVarType::INTEGER};
    int64_t value{0};          // INTEGER (sign-extended) or LONG; 0 otherwise
    const std::string *text{nullptr};  // STRING base only: the default string
};

// VarPlayerType: one player-domain var ("varp") definition. JS5 index 2
// (config), group 60 (VAR_PLAYER), one file per varp id.
//
// Decoded exactly as rs2client does (build 950-1, VarType__DecodeOpcode at
// RVA 0x3F6F30). The field comments name the client VarType offset each opcode
// writes, because only opcode 3 has an established meaning; the rest are
// carried raw.
//
//   op 3   g1  ScriptVarType id (a type ID, not a type char). The client looks it
//              up in its id-keyed ScriptVarType table and stores null if the id is
//              unknown.                                        -> VarType+0x40
//   op 4   g1  raw, meaning not established                    -> VarType+0x48
//   op 5   g1  raw, meaning not established                    -> VarType+0x49
//   op 7   --  clears a flag the loader initialises to 1       -> VarType+0x50
//   op 8   --  sets a flag the loader initialises to 0         -> VarType+0x51
//   op 110 g2  raw big-endian u16, meaning not established     -> VarType+0x4C
//   op 0       terminator
class VarPlayerType
{
public:
    int id{-1};
    int typeId{-1};            // op 3; -1 when absent
    bool hasOp4{false};
    uint8_t op4{0};
    bool hasOp5{false};
    uint8_t op5{0};
    bool hasOp110{false};
    uint16_t op110{0};
    bool flagOp7{true};        // VarType+0x50: true unless op 7 is present
    bool flagOp8{false};       // VarType+0x51: true iff op 8 is present

    // Strict decode over exactly [data, data + size). Returns false on an
    // unknown opcode, a truncated operand, a missing terminator, or trailing
    // bytes after the terminator; *this is then left default (only id kept).
    bool decodeStrict(const uint8_t *data, size_t size);

    // RSBuffer adapter for the generic config loaders: decodes the remaining
    // bytes strictly and consumes them. On failure the entry stays default.
    void decode(RSBuffer &buffer);

    // The ScriptVarType opcode 3 named, or null (absent or not in the table).
    [[nodiscard]] const ScriptVarType *scriptVarType() const;

    // True when the client's player-domain default rule substitutes the
    // domain's own default (-1) for the type's default. Mirrors
    // VarDomainType__GetDefaultVarValue (RVA 0x32B5B0), the vtable slot used by
    // g_VarDomainType_Player: flag +0x50 set AND type == BOOLEAN.
    [[nodiscard]] bool usesDomainDefault() const;

    // The value the client reads for this varp while the server has not set
    // it: usesDomainDefault() ? -1 : the ScriptVarType's default.
    [[nodiscard]] VarpDefault resolveDefault() const;
};
