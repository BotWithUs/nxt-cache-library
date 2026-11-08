#include "dumper/ClientScriptDump.h"

#include "config_types/ClientScript.h"
#include "core/Archive.h"
#include "dumper/Json.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace nxtdump {

namespace {

using json = nlohmann::json;

std::optional<cs2::OpcodeTable> loadOpcodeTable(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        std::cerr << "Cannot open --opcodes file " << path << "\n";
        return std::nullopt;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    auto table = cs2::parseOpcodeTable(text, &error);
    if (!table)
    {
        std::cerr << "Bad --opcodes file " << path << ": " << error << "\n";
    }
    return table;
}

// The script ids to visit: the one asked for, else every group in index 12.
std::vector<int> scriptIds(CacheSource &cache, const ClientScriptDumpOptions &options)
{
    std::vector<int> ids;
    if (options.id)
    {
        ids.push_back(*options.id);
    }
    else
    {
        ids = cache.archiveIds(cs2::kClientScriptIndex);
    }
    if (options.limit && ids.size() > static_cast<size_t>(std::max(0, *options.limit)))
    {
        ids.resize(static_cast<size_t>(std::max(0, *options.limit)));
    }
    return ids;
}

// Reads and decodes one script, then evicts its group. nullopt when the group
// or its file is absent.
std::optional<cs2::ClientScript> loadScript(CacheSource &cache, int id, const cs2::OpcodeTable *table)
{
    Archive &archive = cache.archive(cs2::kClientScriptIndex, id);
    if (archive.id == -1)
    {
        return std::nullopt;
    }
    RSBuffer buffer = archive.readFile(0);
    std::optional<cs2::ClientScript> script;
    if (buffer.buffer != nullptr && buffer.remaining() != 0)
    {
        const auto *data = reinterpret_cast<const uint8_t *>(buffer.buffer + buffer.readPosition);
        script = cs2::decodeClientScript(id, data, buffer.remaining(), table);
    }
    cache.evictArchive(cs2::kClientScriptIndex, id);
    return script;
}

std::string assemblyListing(const cs2::ClientScript &script, const cs2::OpcodeTable *table)
{
    std::ostringstream out;
    out << "; script " << script.id;
    if (script.name)
    {
        out << " " << *script.name;
    }
    out << "\n; args int=" << script.intArgCount << " string=" << script.stringArgCount
        << " long=" << script.longArgCount << "  locals int=" << script.intLocalCount
        << " string=" << script.stringLocalCount << " long=" << script.longLocalCount
        << "  instructions=" << script.declaredInstructionCount << "\n";
    out << cs2::toAssembly(script, table);
    for (size_t i = 0; i < script.switches.size(); i++)
    {
        out << "; switch " << i << ":";
        for (const cs2::SwitchCase &c : script.switches[i].cases)
        {
            out << " " << c.value << "->" << c.target;
        }
        out << "\n";
    }
    if (!script.isComplete())
    {
        out << "; ERROR " << cs2::decodeFailureName(script.failure) << ": " << script.error << "\n";
    }
    return out.str();
}

std::string renderScript(const cs2::ClientScript &script, const cs2::OpcodeTable *table,
                         const ClientScriptDumpOptions &options)
{
    if (options.isJson)
    {
        return toJson(script, table).dump(options.isPretty ? 2 : -1, ' ', false,
                                          json::error_handler_t::replace);
    }
    return assemblyListing(script, table);
}

// Opcodes of interest found in one script: opcode -> use count.
std::map<int, int> grepHits(const cs2::ClientScript &script, const std::set<int> &wanted)
{
    std::map<int, int> hits;
    for (const cs2::Instruction &insn : script.instructions)
    {
        if (wanted.count(insn.opcode) != 0)
        {
            hits[insn.opcode]++;
        }
    }
    return hits;
}

struct Tally
{
    long scripts{0};
    long complete{0};
    long missing{0};
    long matched{0};
    std::map<std::string, long> failures;   // reason -> scripts
    std::map<int, long> unknownOpcodes;     // opcode that stopped a decode -> scripts

    void add(const cs2::ClientScript &script)
    {
        scripts++;
        if (script.isComplete())
        {
            complete++;
            return;
        }
        failures[std::string(cs2::decodeFailureName(script.failure))]++;
        if (script.failure == cs2::DecodeFailure::UnknownOpcode && !script.instructions.empty())
        {
            unknownOpcodes[script.instructions.back().opcode]++;
        }
    }

    void print(double seconds, bool isGrep) const
    {
        std::cerr << "clientscript: " << scripts << " scripts, " << complete << " complete, "
                  << (scripts - complete) << " failed, " << missing << " missing";
        if (isGrep)
        {
            std::cerr << ", " << matched << " matched";
        }
        std::fprintf(stderr, ", %.2f s\n", seconds);
        for (const auto &[reason, count] : failures)
        {
            std::cerr << "  failed " << reason << ": " << count << "\n";
        }
        int shown = 0;
        for (const auto &[opcode, count] : unknownOpcodes)
        {
            if (shown++ == 20)
            {
                std::cerr << "  ... " << (unknownOpcodes.size() - 20) << " more unknown opcodes\n";
                break;
            }
            std::cerr << "  unknown opcode " << opcode << ": stops " << count << " scripts\n";
        }
    }
};

class Sink
{
    std::ofstream file_;
    std::ostream *out_{&std::cout};
public:
    bool open(const std::string &path)
    {
        if (path.empty())
        {
            return true;
        }
        file_.open(path, std::ios::binary);
        out_ = &file_;
        return static_cast<bool>(file_);
    }

    std::ostream &stream() { return *out_; }
};

}  // namespace

int runClientScriptDump(CacheSource &cache, const ClientScriptDumpOptions &options)
{
    std::optional<cs2::OpcodeTable> table;
    if (!options.opcodesPath.empty())
    {
        table = loadOpcodeTable(options.opcodesPath);
        if (!table)
        {
            return 2;
        }
    }
    const cs2::OpcodeTable *tablePtr = table ? &*table : nullptr;
    const std::set<int> wanted(options.grepOps.begin(), options.grepOps.end());
    const bool isGrep = !wanted.empty();

    if (!options.outDir.empty() && !isGrep)
    {
        std::error_code ec;
        std::filesystem::create_directories(options.outDir, ec);
        if (ec)
        {
            std::cerr << "Cannot create --out-dir " << options.outDir << ": " << ec.message() << "\n";
            return 1;
        }
    }
    Sink sink;
    if (!sink.open(options.outPath))
    {
        std::cerr << "Cannot write " << options.outPath << "\n";
        return 1;
    }
    std::ostream &out = sink.stream();

    const auto started = std::chrono::steady_clock::now();
    Tally tally;
    bool isFirstJson = true;
    // --grep-op always reports to one stream; --out-dir applies to listings only.
    const bool isJsonStream = options.isJson && (options.outDir.empty() || isGrep);
    if (isJsonStream)
    {
        out << "[";
    }

    for (int id : scriptIds(cache, options))
    {
        std::optional<cs2::ClientScript> script = loadScript(cache, id, tablePtr);
        if (!script)
        {
            tally.missing++;
            if (options.id)
            {
                std::cerr << "clientscript " << id << " not found\n";
            }
            continue;
        }
        tally.add(*script);

        if (isGrep)
        {
            std::map<int, int> hits = grepHits(*script, wanted);
            if (hits.empty())
            {
                continue;
            }
            tally.matched++;
            if (options.isJson)
            {
                json entry = {{"id", script->id}, {"complete", script->isComplete()}};
                json uses = json::object();
                for (const auto &[opcode, count] : hits)
                {
                    uses[std::to_string(opcode)] = count;
                }
                entry["uses"] = std::move(uses);
                out << (isFirstJson ? "\n" : ",\n") << entry.dump();
                isFirstJson = false;
                continue;
            }
            out << script->id;
            for (const auto &[opcode, count] : hits)
            {
                const cs2::OpcodeSpec *spec = tablePtr ? tablePtr->find(opcode) : nullptr;
                out << "  " << opcode;
                if (spec && !spec->name.empty())
                {
                    out << ":" << spec->name;
                }
                out << "x" << count;
            }
            out << (script->isComplete() ? "" : "  (partial decode)") << "\n";
            continue;
        }

        std::string text = renderScript(*script, tablePtr, options);
        if (!options.outDir.empty())
        {
            auto path = std::filesystem::path(options.outDir) /
                        (std::to_string(id) + (options.isJson ? ".json" : ".cs2asm"));
            std::ofstream file(path, std::ios::binary);
            file << text << "\n";
            if (!file)
            {
                std::cerr << "Cannot write " << path.string() << "\n";
                return 1;
            }
        }
        else if (isJsonStream)
        {
            out << (isFirstJson ? "\n" : ",\n") << text;
            isFirstJson = false;
        }
        else
        {
            out << text << "\n";
        }
    }

    if (isJsonStream)
    {
        out << "\n]\n";
    }
    out.flush();
    const double seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    tally.print(seconds, isGrep);
    if (options.id && tally.missing != 0)
    {
        return 1;
    }
    return out ? 0 : 1;
}

}  // namespace nxtdump
