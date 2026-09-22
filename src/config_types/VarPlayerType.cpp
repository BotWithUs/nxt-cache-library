#include "config_types/VarPlayerType.h"

#include <variant>

namespace
{

constexpr int kScriptVarTypeBoolean = 1;

}  // namespace

bool VarPlayerType::decodeStrict(const uint8_t *data, size_t size)
{
    VarPlayerType parsed{};
    parsed.id = id;
    size_t pos = 0;
    while (true)
    {
        if (pos >= size)
        {
            return false;
        }
        const uint8_t opcode = data[pos++];
        if (opcode == 0)
        {
            break;
        }
        if (opcode == 3 || opcode == 4 || opcode == 5)
        {
            if (pos + 1 > size)
            {
                return false;
            }
            const uint8_t value = data[pos++];
            if (opcode == 3)
            {
                parsed.typeId = value;
            }
            else if (opcode == 4)
            {
                parsed.hasOp4 = true;
                parsed.op4 = value;
            }
            else
            {
                parsed.hasOp5 = true;
                parsed.op5 = value;
            }
        }
        else if (opcode == 110)
        {
            if (pos + 2 > size)
            {
                return false;
            }
            parsed.hasOp110 = true;
            parsed.op110 = static_cast<uint16_t>((data[pos] << 8) | data[pos + 1]);
            pos += 2;
        }
        else if (opcode == 7)
        {
            parsed.flagOp7 = false;
        }
        else if (opcode == 8)
        {
            parsed.flagOp8 = true;
        }
        else
        {
            return false;
        }
    }
    if (pos != size)
    {
        return false;
    }
    *this = parsed;
    return true;
}

void VarPlayerType::decode(RSBuffer &buffer)
{
    const size_t remaining = buffer.remaining();
    const auto *bytes = reinterpret_cast<const uint8_t *>(buffer.buffer + buffer.readPosition);
    decodeStrict(bytes, remaining);
    buffer.readPosition += static_cast<unsigned int>(remaining);
}

const ScriptVarType *VarPlayerType::scriptVarType() const
{
    if (typeId < 0)
    {
        return nullptr;
    }
    return ScriptVarType::getScriptVarTypeById(typeId);
}

bool VarPlayerType::usesDomainDefault() const
{
    return flagOp7 && typeId == kScriptVarTypeBoolean;
}

VarpDefault VarPlayerType::resolveDefault() const
{
    VarpDefault out{};
    const ScriptVarType *svt = scriptVarType();
    if (svt == nullptr)
    {
        return out;
    }
    out.hasBaseType = true;
    out.baseType = svt->getBaseType();
    if (usesDomainDefault())
    {
        out.rule = VarpDefaultRule::Domain;
        out.value = -1;
        return out;
    }
    const DefaultValue &value = svt->getDefaultValue();
    if (const int *asInt = std::get_if<int>(&value))
    {
        out.rule = VarpDefaultRule::Type;
        out.value = *asInt;
    }
    else if (const long long *asLong = std::get_if<long long>(&value))
    {
        out.rule = VarpDefaultRule::Type;
        out.value = *asLong;
    }
    else if (const std::string *asText = std::get_if<std::string>(&value))
    {
        out.rule = VarpDefaultRule::Type;
        out.text = asText;
    }
    return out;
}
