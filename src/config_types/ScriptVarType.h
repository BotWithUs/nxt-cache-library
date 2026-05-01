#pragma once

#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

enum class BaseVarType
{
    INTEGER,
    LONG,
    STRING,
    COORDFINE
};

using DefaultValue = std::variant<int, long long, std::string, std::nullptr_t>;

class ScriptVarType
{
public:
    int id;
    std::string name;
    char32_t c;
    BaseVarType baseType;
    DefaultValue defaultValue;

    ScriptVarType(int id, std::string name, char32_t c, BaseVarType base, DefaultValue defVal)
        : id(id), name(std::move(name)), c(c), baseType(base), defaultValue(std::move(defVal))
    {}

    ScriptVarType(int id, char32_t c, BaseVarType base, DefaultValue defVal)
        : id(id), name("UNKNOWN" + std::to_string(id)), c(c), baseType(base), defaultValue(std::move(defVal))
    {}

    [[nodiscard]] int getId() const { return id; }
    [[nodiscard]] const std::string &getName() const { return name; }
    [[nodiscard]] char32_t getC() const { return c; }
    [[nodiscard]] BaseVarType getBaseType() const { return baseType; }
    [[nodiscard]] const DefaultValue &getDefaultValue() const { return defaultValue; }
    [[nodiscard]] std::string toString() const { return name; }

    static ScriptVarType *getScriptVarTypeById(int id);
    static ScriptVarType *getByChar(char32_t c);
    static char32_t cp1252ToUnicode(char cp1252Char);

private:
    static std::unordered_map<int, const ScriptVarType *> scriptVarTypes;
    static bool initialized;

    static void init();
};
