// ClientScript (CS2) decoder tests.
//
//   nxtcache-clientscript-test --offline     synthetic scripts, no cache
//   nxtcache-clientscript-test [cache-dir]   every script in a real cache
//
// --offline round-trips every operand encoding through an independent encoder,
// checks the stop-and-report paths (unknown opcode, no table, count mismatch,
// truncation) and feeds every truncation and single-byte corruption of a valid
// script to the decoder, which must return without crashing.
//
// The cache mode decodes all of index 12 with the 951-3 operand-width table
// (built below from the 951-3 exceptions), or with NXTCACHE_CS2_OPCODES when
// set. The index-12 reference table carries no version, so the pinned 951-3
// cache is recognised by a fingerprint of every group's (id, CRC). Every
// script must decode completely on the pinned cache or with a caller table;
// on any other cache the built-in widths are stale (a client update reshuffles
// opcode numbers), so the sweep runs as a no-crash check and completeness is
// reported as skipped. The per-script pins and corpus counts, cross-checked
// against the RE peer's independent reader, run only on the pinned cache.
// testGates proves that gating on the real cache with a moved opcode table.
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 pass, 1 a check failed, 77 cache absent (SKIP).

#include "c_api/nxtcache_c.h"
#include "config_types/ClientScript.h"
#include "core/RSCache.h"
// RSCache's destructor needs the JS5 types it owns by unique_ptr complete.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <nlohmann/json.hpp>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{

constexpr int kExitSkip = 77;

int failures = 0;
int skipped = 0;
bool isQuiet = false;  // set while a check runs in a sandbox (see sandboxedFailures)

void report(const std::string &line)
{
    if (!isQuiet)
    {
        std::printf("  %s\n", line.c_str());
    }
}

void check(bool isOk, const std::string &what)
{
    if (!isOk)
    {
        report("FAIL  " + what);
        failures++;
    }
}

void checkLoud(bool isOk, const std::string &what)
{
    report(std::string(isOk ? "ok    " : "FAIL  ") + what);
    if (!isOk)
    {
        failures++;
    }
}

void skipCheck(const std::string &what)
{
    report("SKIPPED CHECK  " + what);
    skipped++;
}

// ---- An encoder written from the format notes, independent of the decoder --

constexpr int kOpByte = 1;
constexpr int kOpInt = 2;
constexpr int kOpVar = 3;
constexpr int kOpVarbit = 4;
constexpr int kOpConst = 5;
constexpr int kOpUnknown = 99;

const char *kSyntheticTable = R"([
    {"opcode": 1, "operand": "byte", "name": "op_byte"},
    {"opcode": 2, "operand": "int", "name": "op_int"},
    {"opcode": 3, "operand": "var"},
    {"opcode": 4, "operand": "varbit", "name": "op_varbit", "extra": [1, 2]},
    {"opcode": 5, "operand": "typed_constant", "name": "op_const"}
])";

class Encoder
{
public:
    std::vector<uint8_t> bytes;

    void u8(uint32_t v) { bytes.push_back(static_cast<uint8_t>(v)); }
    void u16(uint32_t v) { u8(v >> 8); u8(v); }
    void u24(uint32_t v) { u8(v >> 16); u16(v & 0xFFFF); }
    void u32(uint32_t v) { u16(v >> 16); u16(v & 0xFFFF); }
    void i64(int64_t v) { u32(static_cast<uint32_t>(static_cast<uint64_t>(v) >> 32)); u32(static_cast<uint32_t>(v)); }

    void cstr(const std::string &raw)  // raw cp1252 bytes
    {
        bytes.insert(bytes.end(), raw.begin(), raw.end());
        u8(0);
    }
};

struct SyntheticScript
{
    std::string rawName;
    Encoder stream;
    int count{0};
    std::vector<std::vector<std::pair<int32_t, int32_t>>> switches;
    uint16_t locals[3]{4, 5, 6};
    uint16_t args[3]{1, 2, 3};

    std::vector<uint8_t> build(int declaredCount) const
    {
        Encoder out;
        out.cstr(rawName);
        out.bytes.insert(out.bytes.end(), stream.bytes.begin(), stream.bytes.end());
        out.u32(static_cast<uint32_t>(declaredCount));
        for (uint16_t v : locals)
        {
            out.u16(v);
        }
        for (uint16_t v : args)
        {
            out.u16(v);
        }
        const size_t switchStart = out.bytes.size();
        out.u8(static_cast<uint32_t>(switches.size()));
        for (const auto &table : switches)
        {
            out.u16(static_cast<uint32_t>(table.size()));
            for (const auto &[key, target] : table)
            {
                out.u32(static_cast<uint32_t>(key));
                out.u32(static_cast<uint32_t>(target));
            }
        }
        out.u16(static_cast<uint32_t>(out.bytes.size() - switchStart));
        return out.bytes;
    }

    std::vector<uint8_t> build() const { return build(count); }

    void op(int opcode) { stream.u16(static_cast<uint32_t>(opcode)); count++; }
};

// One script with every operand encoding at its edge values.
SyntheticScript everyOperandScript()
{
    SyntheticScript s;
    s.rawName = "cs\x80name";  // 0x80 is the euro sign in cp1252
    s.op(kOpByte);   s.stream.u8(0);
    s.op(kOpByte);   s.stream.u8(255);
    s.op(kOpInt);    s.stream.u32(static_cast<uint32_t>(INT32_MIN));
    s.op(kOpInt);    s.stream.u32(static_cast<uint32_t>(INT32_MAX));
    s.op(kOpInt);    s.stream.u32(0xFFFFFFFFu);
    s.op(kOpVar);    s.stream.u8(255); s.stream.u16(65535); s.stream.u8(1);
    s.op(kOpVarbit); s.stream.u24(0xFFFFFF); s.stream.u8(1);
    s.op(kOpConst);  s.stream.u8(0); s.stream.u32(static_cast<uint32_t>(-7));
    s.op(kOpConst);  s.stream.u8(1); s.stream.i64(INT64_MIN);
    s.op(kOpConst);  s.stream.u8(1); s.stream.i64(INT64_MAX);
    s.op(kOpConst);  s.stream.u8(2); s.stream.cstr("a\x93q\x94");
    s.op(kOpConst);  s.stream.u8(2); s.stream.cstr("");
    s.op(kOpConst);  s.stream.u8(7);
    s.switches = {{{1, 2}, {INT32_MIN, INT32_MAX}}, {}, {{-1, -2}}};
    return s;
}

cs2::OpcodeTable syntheticTable()
{
    std::string error;
    auto table = cs2::parseOpcodeTable(kSyntheticTable, &error);
    if (!table)
    {
        std::printf("  FAIL  synthetic table: %s\n", error.c_str());
        std::exit(1);
    }
    return *table;
}

cs2::ClientScript decode(const std::vector<uint8_t> &bytes, const cs2::OpcodeTable *table)
{
    return cs2::decodeClientScript(7, bytes.data(), bytes.size(), table);
}

void testRoundTrip(const cs2::OpcodeTable &table)
{
    const SyntheticScript s = everyOperandScript();
    const cs2::ClientScript d = decode(s.build(), &table);
    checkLoud(d.isComplete(), "every operand encoding decodes completely: " + d.error);
    checkLoud(d.instructions.size() == 13, "13 instructions");
    if (d.instructions.size() != 13)
    {
        return;
    }
    const auto &in = d.instructions;
    check(d.name && *d.name == "cs\xE2\x82\xACname", "cp1252 name -> UTF-8");
    check(in[0].intOperand == 0 && in[1].intOperand == 255, "byte 0 / 255 (zero-extended)");
    check(in[2].intOperand == INT32_MIN && in[3].intOperand == INT32_MAX && in[4].intOperand == -1,
          "int min / max / -1");
    check(in[5].operand == cs2::OperandKind::Var && in[5].varDomain == 255 && in[5].varId == 65535 &&
              in[5].intOperand == 1,
          "var domain u8, id u16, flag u8");
    check(in[6].operand == cs2::OperandKind::Varbit && in[6].varId == 0xFFFFFF && in[6].intOperand == 1,
          "varbit id u24, flag u8");
    check(in[7].constant == cs2::ConstantType::Int && in[7].intOperand == -7, "typed constant 0: i32");
    check(in[8].constant == cs2::ConstantType::Long && in[8].intOperand == INT64_MIN, "typed constant 1: i64 min");
    check(in[9].constant == cs2::ConstantType::Long && in[9].intOperand == INT64_MAX, "typed constant 1: i64 max");
    check(in[10].constant == cs2::ConstantType::String &&
              in[10].stringOperand == "a\xE2\x80\x9Cq\xE2\x80\x9D",
          "typed constant 2: cp1252 string -> UTF-8");
    check(in[11].constant == cs2::ConstantType::String && in[11].stringOperand.empty(), "empty string");
    check(in[12].constant == cs2::ConstantType::Other && in[12].intOperand == 7, "typed constant 3+: no value");

    // Byte offsets: name "cs?name\0" is 8 bytes; then 2+1, 2+1, 2+4 x3, 2+4, 2+4, ...
    const uint32_t expected[] = {8, 11, 14, 20, 26, 32, 38, 44, 51, 62, 73, 81, 85};
    bool isOffsetsOk = true;
    for (size_t i = 0; i < 13; i++)
    {
        isOffsetsOk = isOffsetsOk && in[i].byteOffset == expected[i] && in[i].index == i;
    }
    check(isOffsetsOk, "instruction index and byte offset");

    check(d.intLocalCount == 4 && d.stringLocalCount == 5 && d.longLocalCount == 6, "locals int/string/long");
    check(d.intArgCount == 1 && d.stringArgCount == 2 && d.longArgCount == 3, "args int/string/long");
    check(d.switches.size() == 3 && d.switches[0].cases.size() == 2 && d.switches[1].cases.empty() &&
              d.switches[0].cases[1].value == INT32_MIN && d.switches[0].cases[1].target == INT32_MAX &&
              d.switches[2].cases[0].value == -1 && d.switches[2].cases[0].target == -2,
          "switch tables (including an empty one)");

    const std::string text = cs2::toAssembly(d, &table);
    check(text.find("    5      3  var 255:65535 flag 1\n") != std::string::npos,
          "assembly: an opcode the table does not name prints raw");
    check(text.find("    6      4  op_varbit  varbit 16777215 flag 1\n") != std::string::npos,
          "assembly: named varbit");
    check(text.find("    8      5  op_const  long -9223372036854775808\n") != std::string::npos,
          "assembly: long constant");
    check(cs2::toAssembly(d, nullptr).find("op_") == std::string::npos, "assembly without a table: no names");
}

void testStops(const cs2::OpcodeTable &table)
{
    SyntheticScript s;
    s.op(kOpByte);    s.stream.u8(9);
    s.op(kOpUnknown); s.stream.u8(0);
    s.op(kOpByte);    s.stream.u8(9);
    cs2::ClientScript d = decode(s.build(), &table);
    checkLoud(d.failure == cs2::DecodeFailure::UnknownOpcode && d.instructions.size() == 2 &&
                  !d.instructions[1].isKnown && d.instructions[1].opcode == kOpUnknown &&
                  d.instructions[0].intOperand == 9,
              "unknown opcode: kept raw, earlier instructions kept, decode stops");
    check(cs2::toAssembly(d, &table).find("   99  ??\n") != std::string::npos, "unknown opcode prints as ??");

    d = decode(s.build(), nullptr);
    checkLoud(d.failure == cs2::DecodeFailure::NoOpcodeTable && d.instructions.empty() &&
                  d.declaredInstructionCount == 3 && d.intLocalCount == 4,
              "no table: footer decoded, instructions not");

    SyntheticScript c;
    c.op(kOpInt); c.stream.u32(1);
    d = decode(c.build(2), &table);
    checkLoud(d.failure == cs2::DecodeFailure::CountMismatch && d.instructions.size() == 1,
              "footer count disagrees with the stream");

    SyntheticScript t;
    t.op(kOpInt); t.stream.u16(1);  // an int operand cut to 2 bytes
    d = decode(t.build(), &table);
    checkLoud(d.failure == cs2::DecodeFailure::Truncated && d.instructions.empty(),
              "operand cut off by the footer");

    SyntheticScript n;
    n.rawName = "x";
    std::vector<uint8_t> noTerminator = n.build();
    noTerminator[1] = 'y';  // the name's NUL, now nothing terminates it before F
    d = decode(noTerminator, &table);
    checkLoud(d.failure == cs2::DecodeFailure::Truncated, "name without a terminator");

    const uint8_t tiny[] = {0, 1};
    d = cs2::decodeClientScript(1, tiny, sizeof(tiny), &table);
    checkLoud(d.failure == cs2::DecodeFailure::BadFooter, "file shorter than a footer");
    d = cs2::decodeClientScript(1, nullptr, 0, &table);
    checkLoud(d.failure == cs2::DecodeFailure::BadFooter, "empty input");
}

// Every prefix and every single-byte corruption of a valid script must decode
// to *something* without crashing or reading out of bounds.
void testMalformed(const cs2::OpcodeTable &table)
{
    const std::vector<uint8_t> good = everyOperandScript().build();
    long decodes = 0;
    long failed = 0;
    for (size_t len = 0; len < good.size(); len++)
    {
        // A heap copy of exactly len bytes, so an over-read hits the allocation edge.
        std::vector<uint8_t> prefix(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(len));
        failed += decode(prefix, &table).isComplete() ? 0 : 1;
        decodes++;
    }
    checkLoud(failed == decodes, "every strict prefix fails cleanly (" + std::to_string(decodes) + " prefixes)");

    const uint8_t patterns[] = {0x00, 0xFF, 0x7F, 0x80};
    decodes = 0;
    for (size_t pos = 0; pos < good.size(); pos++)
    {
        for (uint8_t pattern : patterns)
        {
            std::vector<uint8_t> bad = good;
            bad[pos] = pattern;
            (void)decode(bad, &table);
            bad[pos] = static_cast<uint8_t>(good[pos] ^ pattern);
            (void)decode(bad, &table);
            decodes += 2;
        }
    }
    checkLoud(true, "single-byte corruptions decoded without a crash (" + std::to_string(decodes) + ")");
}

void testTableParse()
{
    std::string error;
    checkLoud(!cs2::parseOpcodeTable("{", &error) && !error.empty(), "table: malformed JSON refused");
    checkLoud(!cs2::parseOpcodeTable(R"({"opcodes": []})", &error), "table: non-array refused");
    checkLoud(!cs2::parseOpcodeTable(R"([{"opcode": 1, "operand": "word"}])", &error), "table: unknown kind refused");
    checkLoud(!cs2::parseOpcodeTable(R"([{"opcode": 1, "operand": "byte"}, {"opcode": 1, "operand": "int"}])",
                                     &error),
              "table: duplicate opcode refused");
    checkLoud(!cs2::parseOpcodeTable(R"([{"opcode": 70000, "operand": "byte"}])", &error),
              "table: non-u16 opcode refused");
    checkLoud(!cs2::parseOpcodeTable(R"([{"operand": "byte"}])", &error), "table: missing opcode refused");

    // Values that used to narrow silently to opcode 1.
    const char *badOpcodes[] = {"4294967297", "1.9", "true", "-1", "\"5\"", "65536", "18446744073709551615", "null"};
    for (const char *bad : badOpcodes)
    {
        const std::string doc = std::string(R"([{"opcode": )") + bad + R"(, "operand": "byte"}])";
        error.clear();
        checkLoud(!cs2::parseOpcodeTable(doc, &error) && error.find("\"opcode\" must be an integer") != std::string::npos,
                  std::string("table: opcode ") + bad + " refused with a clear error");
    }
    checkLoud(!cs2::parseOpcodeTable(R"([{"opcode": 1, "operand": 3}])", &error), "table: non-string operand refused");
    checkLoud(!cs2::parseOpcodeTable(R"([{"opcode": 1, "operand": "byte", "name": 7}])", &error),
              "table: non-string name refused");
    checkLoud(!cs2::parseOpcodeTable(R"([1])", &error), "table: non-object entry refused");
    auto edges = cs2::parseOpcodeTable(
        R"([{"opcode": 0, "operand": "byte", "name": null}, {"opcode": 65535, "operand": "int"}])", &error);
    checkLoud(edges && edges->ops.size() == 2 && edges->find(65535) && edges->find(0)->name.empty(),
              "table: opcodes 0 and 65535 accepted, null name allowed");
}

// Defined with the cache checks below.
void pinScripts(const std::map<int, cs2::ClientScript> &pinned);

// The pins index only what was decoded: short or missing scripts are FAILs.
void testPinsNeverThrow()
{
    std::map<int, cs2::ClientScript> shortDecodes;
    for (int id : {0, 2, 3, 5, 426})
    {
        cs2::ClientScript s;
        s.id = id;
        s.instructions.resize(1);
        shortDecodes.emplace(id, std::move(s));
    }
    const int before = failures;
    isQuiet = true;
    pinScripts({});
    const int onEmpty = failures - before;
    pinScripts(shortDecodes);
    const int onShort = failures - before - onEmpty;
    isQuiet = false;
    failures = before;
    checkLoud(onEmpty == 5 && onShort == 5, "pins on missing / one-instruction scripts: 5 FAILs each, no throw");
}

void testCApi()
{
    const std::vector<uint8_t> good = everyOperandScript().build();
    char *json = nullptr;
    size_t len = 0;
    nxt_cs2_table *table = nxt_cs2_table_open(kSyntheticTable);
    checkLoud(table != nullptr, "C ABI: table opens");
    nxt_result rc = nxt_decode_clientscript_json(good.data(), good.size(), 7, table, &json, &len);
    checkLoud(rc == NXT_OK && json != nullptr, "C ABI: decode returns NXT_OK");
    if (rc == NXT_OK)
    {
        auto doc = nlohmann::json::parse(json, json + len);
        check(doc["complete"] == true && doc["instructions"].size() == 13 && doc["name"] == "cs\xE2\x82\xACname",
              "C ABI: JSON complete, 13 instructions, name");
        check(doc["instructions"][5]["operand"]["id"] == 65535 && doc["instructions"][8]["constantType"] == "long" &&
                  doc["instructions"][0]["name"] == "op_byte" && !doc["instructions"][5].contains("name"),
              "C ABI: var operand, constant type, names only from the table");
        nxt_free(json);
    }
    json = nullptr;
    rc = nxt_decode_clientscript_json(good.data(), good.size(), 7, nullptr, &json, &len);
    check(rc == NXT_OK && std::string(json, len).find("\"failure\":\"no_opcode_table\"") != std::string::npos,
          "C ABI: no table is NXT_OK with failure no_opcode_table");
    nxt_free(json);
    for (int i = 0; i < 3; i++)
    {
        rc = nxt_decode_clientscript_json(good.data(), good.size(), 7, table, &json, &len);
        check(rc == NXT_OK && std::string(json, len).find("\"complete\":true") != std::string::npos,
              "C ABI: one table serves repeated decodes");
        nxt_free(json);
    }
    nxt_cs2_table_free(table);
    nxt_cs2_table_free(nullptr);
    checkLoud(nxt_cs2_table_open("[{") == nullptr && std::string(nxt_last_error()).find("bad opcodes_json") == 0,
              "C ABI: malformed table refused at open, with nxt_last_error");
    checkLoud(nxt_cs2_table_open(R"([{"opcode": 1.9, "operand": "byte"}])") == nullptr,
              "C ABI: non-integer opcode refused at open");
    checkLoud(nxt_cs2_table_open(nullptr) == nullptr, "C ABI: null opcodes_json refused");
    check(nxt_decode_clientscript_json(nullptr, 4, 7, nullptr, &json, &len) == NXT_ERR_INVALID,
          "C ABI: null data with a size is NXT_ERR_INVALID");
    check(nxt_get_clientscript_json(nullptr, 0, nullptr, &json, &len) == NXT_ERR_INVALID,
          "C ABI: null cache is NXT_ERR_INVALID");
}

int runOffline()
{
    std::printf("clientscript offline\n");
    const cs2::OpcodeTable table = syntheticTable();
    testRoundTrip(table);
    testStops(table);
    testMalformed(table);
    testTableParse();
    testPinsNeverThrow();
    testCApi();
    std::printf("%s (%d failed)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}

// ---- Real cache -----------------------------------------------------------

// The 951-3 operand widths: every opcode 0..2334 is byte unless listed here.
// Numbers only; opcode names stay out of the library and its tests. Used
// only to decode; completeness is enforced with it only on the pinned cache.
cs2::OpcodeTable table951_3()
{
    const std::set<int> intOps = {224,  352,  358,  412,  463,  495,  567,  606,  643,  645,  663,
                                  667,  886,  929,  932,  1103, 1198, 1372, 1468, 1497, 1514, 1565,
                                  1692, 1762, 1790, 1844, 2007, 2012, 2103, 2109, 2226, 2255};
    cs2::OpcodeTable table;
    table.revision = "951-3";
    for (int op = 0; op <= 2334; op++)
    {
        table.ops[op].operand = intOps.count(op) != 0 ? cs2::OperandKind::Int : cs2::OperandKind::Byte;
    }
    table.ops[1460].operand = cs2::OperandKind::Var;
    table.ops[1814].operand = cs2::OperandKind::Var;
    table.ops[57].operand = cs2::OperandKind::Varbit;
    table.ops[1764].operand = cs2::OperandKind::Varbit;
    table.ops[641].operand = cs2::OperandKind::TypedConstant;
    return table;
}

// A stand-in for the next client build: the same operand kinds, moved to
// other opcode numbers (each opcode takes the kind of the opcode after it).
cs2::OpcodeTable permuted(const cs2::OpcodeTable &table)
{
    cs2::OpcodeTable out;
    for (auto it = table.ops.begin(); it != table.ops.end(); ++it)
    {
        auto next = std::next(it);
        out.ops[it->first] = next == table.ops.end() ? table.ops.begin()->second : next->second;
    }
    return out;
}

// NXTCACHE_CS2_OPCODES, when set, is the caller's table for this cache.
std::optional<cs2::OpcodeTable> callerTable(bool &outIsBad)
{
    outIsBad = false;
    const char *path = std::getenv("NXTCACHE_CS2_OPCODES");
    if (!path)
    {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto table = cs2::parseOpcodeTable(text, &error);
    if (!table)
    {
        checkLoud(false, std::string("NXTCACHE_CS2_OPCODES ") + path + ": " + error);
        outIsBad = true;
    }
    return table;
}

struct CorpusCounts
{
    long ints{0};
    long strings{0};
    long longs{0};
    long vars{0};
    long varbits{0};
    long switchScripts{0};

    void add(const cs2::ClientScript &s)
    {
        for (const cs2::Instruction &i : s.instructions)
        {
            vars += i.operand == cs2::OperandKind::Var ? 1 : 0;
            varbits += i.operand == cs2::OperandKind::Varbit ? 1 : 0;
            if (i.operand == cs2::OperandKind::TypedConstant)
            {
                ints += i.constant == cs2::ConstantType::Int ? 1 : 0;
                longs += i.constant == cs2::ConstantType::Long ? 1 : 0;
                strings += i.constant == cs2::ConstantType::String ? 1 : 0;
            }
        }
        switchScripts += s.switches.empty() ? 0 : 1;
    }
};

// One decode of all of index 12.
struct Sweep
{
    long scripts{0};
    long incomplete{0};
    uint64_t fingerprint{1469598103934665603ULL};  // FNV-1a over each group's (id, CRC)
    CorpusCounts counts;
    std::map<int, cs2::ClientScript> pinned;
    std::vector<std::string> firstFailures;

    void mix(uint32_t v)
    {
        for (int b = 0; b < 4; b++)
        {
            fingerprint = (fingerprint ^ ((v >> (8 * b)) & 0xFF)) * 1099511628211ULL;
        }
    }
};

const std::set<int> kPinIds = {0, 2, 3, 5, 426};

// The fingerprint of the 951-3 cache the pins and counts were taken from.
constexpr uint64_t kPinnedFingerprint = 0xdf6678075f62b0bcULL;

Sweep sweep(RSCache &cache, const cs2::OpcodeTable &table)
{
    Sweep out;
    for (int id : cache.archiveIds(cs2::kClientScriptIndex))
    {
        Archive &archive = cache.archive(cs2::kClientScriptIndex, id);
        out.mix(static_cast<uint32_t>(id));
        out.mix(static_cast<uint32_t>(archive.crc));
        RSBuffer buffer = archive.readFile(0);
        const auto *data = reinterpret_cast<const uint8_t *>(buffer.buffer + buffer.readPosition);
        cs2::ClientScript s = cs2::decodeClientScript(id, data, buffer.remaining(), &table);
        cache.evictArchive(cs2::kClientScriptIndex, id);
        out.scripts++;
        if (!s.isComplete() && out.incomplete++ < 5)
        {
            out.firstFailures.push_back("script " + std::to_string(id) + ": " +
                                        std::string(cs2::decodeFailureName(s.failure)) + ": " + s.error);
        }
        out.counts.add(s);
        if (kPinIds.count(id) != 0)
        {
            out.pinned.emplace(id, std::move(s));
        }
    }
    return out;
}

// Instruction index of pinned script id, or null when the decode is short of
// it: the pins never index past what was decoded.
const cs2::Instruction *pinnedInsn(const std::map<int, cs2::ClientScript> &pinned, int id, size_t index)
{
    const auto it = pinned.find(id);
    if (it == pinned.end() || index >= it->second.instructions.size())
    {
        return nullptr;
    }
    return &it->second.instructions[index];
}

// Script 0's first 12 instructions as (opcode, operand); 641 = typed constant.
void pinScripts(const std::map<int, cs2::ClientScript> &pinned)
{
    const std::pair<int, int64_t> script0[] = {{1565, 1}, {1565, 2}, {1067, 0}, {641, 2},
                                               {2328, 0}, {1692, 3}, {1565, 3}, {1468, 0},
                                               {1692, 4}, {1565, 3}, {1565, 2}, {1468, 0}};
    const auto s0 = pinned.find(0);
    bool isOk = s0 != pinned.end() && s0->second.declaredInstructionCount == 90 && s0->second.intArgCount == 3 &&
                s0->second.intLocalCount == 8 && s0->second.switches.empty() && !s0->second.name;
    for (size_t i = 0; isOk && i < 12; i++)
    {
        const cs2::Instruction *insn = pinnedInsn(pinned, 0, i);
        isOk = insn && insn->opcode == script0[i].first && insn->intOperand == script0[i].second;
    }
    checkLoud(isOk, "pin: script 0 footer and first 12 instructions");

    const cs2::Instruction *var = pinnedInsn(pinned, 3, 5);
    checkLoud(var && var->opcode == 1814 && var->operand == cs2::OperandKind::Var && var->varDomain == 0 &&
                  var->varId == 4517 && var->intOperand == 0,
              "pin: script 3 #5 var 0:4517 flag 0");
    const cs2::Instruction *varbit = pinnedInsn(pinned, 5, 0);
    checkLoud(varbit && varbit->opcode == 57 && varbit->operand == cs2::OperandKind::Varbit &&
                  varbit->varId == 38842 && varbit->intOperand == 0,
              "pin: script 5 #0 varbit 38842 flag 0");
    const cs2::Instruction *lng = pinnedInsn(pinned, 426, 7);
    checkLoud(lng && lng->opcode == 641 && lng->constant == cs2::ConstantType::Long && lng->intOperand == -1,
              "pin: script 426 #7 long constant -1");
    const auto sw = pinned.find(2);
    checkLoud(sw != pinned.end() && sw->second.switches.size() == 1 && sw->second.switches[0].cases.size() >= 3 &&
                  sw->second.switches[0].cases[0].value == 54533 && sw->second.switches[0].cases[0].target == 1 &&
                  sw->second.switches[0].cases[2].value == 54535 && sw->second.switches[0].cases[2].target == 17,
              "pin: script 2 switch table 0");
}

// The checks a sweep earns. Completeness is enforced only where a failure
// means a decoder bug: on the pinned cache, or with the caller's own table.
// Anywhere else the built-in 951-3 widths are simply stale (the next client
// build reshuffles opcode numbers), so the sweep counts as a no-crash run.
void checkSweep(const Sweep &s, bool isCallerTable, uint64_t pinnedFingerprint)
{
    const bool isPinnedCache = s.fingerprint == pinnedFingerprint;
    const std::string tally = std::to_string(s.scripts) + " scripts, " + std::to_string(s.incomplete) + " incomplete";
    checkLoud(s.scripts > 0, "index 12 has scripts");
    if (isPinnedCache || isCallerTable)
    {
        for (const std::string &failure : s.firstFailures)
        {
            report(failure);
        }
        checkLoud(s.incomplete == 0, "every script decodes completely: " + tally);
    }
    else
    {
        skipCheck("completeness (" + tally + "): not the pinned 951-3 cache and no NXTCACHE_CS2_OPCODES table; "
                  "decoded as a no-crash sweep only");
    }

    if (!isPinnedCache)
    {
        skipCheck("pins and corpus counts: the cache is not the pinned 951-3 index 12");
        return;
    }
    if (s.incomplete != 0)
    {
        checkLoud(false, "pins: " + std::to_string(s.incomplete) +
                             " scripts do not decode with this table, so it does not match the pinned cache");
        return;
    }
    pinScripts(s.pinned);
    checkLoud(s.scripts == 21261, "pin: 21261 scripts");
    checkLoud(s.counts.ints == 436152 && s.counts.strings == 43703 && s.counts.longs == 273,
              "pin: constants int 436152 / string 43703 / long 273");
    checkLoud(s.counts.vars == 29154 && s.counts.varbits == 50911, "pin: var 29154 / varbit 50911 refs");
    checkLoud(s.counts.switchScripts == 4339, "pin: 4339 scripts with switch tables");
}

// checkSweep in a sandbox: its output is suppressed and its failures are
// counted and returned instead of joining the run's.
int sandboxedFailures(const Sweep &s, bool isCallerTable, uint64_t pinnedFingerprint)
{
    const int before = failures;
    const int skippedBefore = skipped;
    isQuiet = true;
    checkSweep(s, isCallerTable, pinnedFingerprint);
    isQuiet = false;
    const int found = failures - before;
    failures = before;
    skipped = skippedBefore;
    return found;
}

// The gate itself, on real data, using a table whose opcode numbers have moved
// as a stand-in for the next client build.
void testGates(RSCache &cache, const cs2::OpcodeTable &table, uint64_t realFingerprint)
{
    const Sweep moved = sweep(cache, permuted(table));
    checkLoud(moved.incomplete > 0, "gate: a moved opcode table leaves scripts incomplete (" +
                                        std::to_string(moved.incomplete) + "), no crash");
    checkLoud(sandboxedFailures(moved, false, realFingerprint ^ 1) == 0,
              "gate: next revision + built-in table -> skipped, not failed");
    checkLoud(sandboxedFailures(moved, true, realFingerprint ^ 1) > 0,
              "gate: next revision + a caller table that does not fit -> FAIL");
    checkLoud(sandboxedFailures(moved, true, realFingerprint) > 0,
              "gate: pinned cache + a mismatched caller table -> FAIL, no abort");
}

int runCache(const std::string &cachePath)
{
    if (!std::filesystem::exists(std::filesystem::path(cachePath) / "js5-12.jcache"))
    {
        std::printf("SKIP: no js5-12.jcache in %s\n", cachePath.c_str());
        return kExitSkip;
    }
    std::printf("clientscript cache %s\n", cachePath.c_str());
    bool isBadTable = false;
    const std::optional<cs2::OpcodeTable> caller = callerTable(isBadTable);
    if (isBadTable)
    {
        return 1;
    }
    const cs2::OpcodeTable table = caller ? *caller : table951_3();

    RSCache cache(cachePath);
    const Sweep s = sweep(cache, table);
    std::printf("  index 12 fingerprint 0x%016llx (pins taken at 0x%016llx)%s\n",
                static_cast<unsigned long long>(s.fingerprint), static_cast<unsigned long long>(kPinnedFingerprint),
                caller ? ", table from NXTCACHE_CS2_OPCODES" : "");
    checkSweep(s, caller.has_value(), kPinnedFingerprint);
    testGates(cache, table, s.fingerprint);
    std::printf("%s (%d failed, %d skipped)\n", failures == 0 ? "PASS" : "FAIL", failures, skipped);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 1 && std::string(argv[1]) == "--offline")
    {
        return runOffline();
    }
    std::string cachePath = R"(C:\ProgramData\Jagex\RuneScape)";
    if (argc > 1)
    {
        cachePath = argv[1];
    }
    else if (const char *env = std::getenv("NXTCACHE_TEST_CACHE"))
    {
        cachePath = env;
    }
    return runCache(cachePath);
}
