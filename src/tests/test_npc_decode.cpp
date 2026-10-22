// Decodes every NPC config in a real cache and requires each one to end exactly on
// its terminating opcode 0 with no unknown opcode. A decoder that reads the wrong
// width for an opcode drifts out of alignment, and the drift shows up here as an
// early stop or an unknown opcode instead of as silently empty names and options.
//
//   nxtcache-npc-decode-test [cache-dir]
//
// cache-dir defaults to $NXTCACHE_TEST_CACHE, then C:\ProgramData\Jagex\RuneScape.
// Exit codes: 0 all checks passed, 1 a check failed, 77 cache absent (SKIP).

#include "config_types/Types.h"
#include "core/Archive.h"
#include "core/RSCache.h"
// RSCache.h only forward-declares these, but its inline constructor needs them
// complete to destroy its unique_ptr members.
#include "network/Js5Config.h"
#include "network/Js5Socket.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace
{

constexpr int kExitSkip = 77;
constexpr int kNpcIndex = 18;
constexpr int kNpcShift = 7;
constexpr int kMaxReported = 20;
constexpr int kHansId = 0;

int failures = 0;

void check(bool isOk, const std::string &what)
{
    std::printf("  %s  %s\n", isOk ? "ok  " : "FAIL", what.c_str());
    if (!isOk)
    {
        failures++;
    }
}

std::string cachePathFrom(int argc, char **argv)
{
    if (argc > 1)
    {
        return argv[1];
    }
    if (const char *env = std::getenv("NXTCACHE_TEST_CACHE"))
    {
        return env;
    }
    return R"(C:\ProgramData\Jagex\RuneScape)";
}

// Empty string when the config decoded cleanly, otherwise why it did not.
std::string decodeProblem(RSBuffer &buffer, NpcType &type)
{
    type.decode(buffer);
    if (type.unknownOpcode != -1)
    {
        return "unknown opcode " + std::to_string(type.unknownOpcode);
    }
    const bool isTerminated = buffer.readPosition > 0 && buffer.buffer[buffer.readPosition - 1] == 0;
    if (buffer.remaining() != 0 || !isTerminated)
    {
        return "stopped at " + std::to_string(buffer.readPosition) + "/" +
               std::to_string(buffer.writePosition);
    }
    return {};
}

void sweepAll(RSCache &cache)
{
    int decoded = 0;
    int failed = 0;
    for (int archiveId : cache.archiveIds(kNpcIndex))
    {
        Archive &archive = cache.archive(kNpcIndex, archiveId);
        if (archive.id == -1)
        {
            continue;
        }
        for (auto &[fileId, unused] : archive.files)
        {
            RSBuffer buffer = archive.readFile(fileId);
            if (buffer.buffer == nullptr || buffer.remaining() == 0)
            {
                continue;
            }
            NpcType type;
            type.id = (archiveId << kNpcShift) | fileId;
            const std::string problem = decodeProblem(buffer, type);
            if (problem.empty())
            {
                decoded++;
                continue;
            }
            if (failed++ < kMaxReported)
            {
                std::printf("    npc %d: %s\n", type.id, problem.c_str());
            }
        }
    }
    std::printf("  npc: decoded %d, failed %d\n", decoded, failed);
    check(decoded > 0, "at least one npc decoded");
    check(failed == 0, "every npc ends on opcode 0 with no unknown opcode");
}

// Hans carries opcode 27 ahead of his name and options, so a decoder without it
// returns him nameless.
void checkHans(RSCache &cache)
{
    Archive &archive = cache.archive(kNpcIndex, kHansId >> kNpcShift);
    RSBuffer buffer = archive.readFile(kHansId & ((1 << kNpcShift) - 1));
    NpcType hans;
    hans.decode(buffer);
    check(hans.name == "Hans", "npc 0 name is Hans (got \"" + hans.name + "\")");
    check(hans.options[0] == "Talk-to" || hans.options[0] == "Talk to",
          "npc 0 first option is Talk to (got \"" + hans.options[0] + "\")");
}

} // namespace

int main(int argc, char **argv)
{
    const std::string path = cachePathFrom(argc, argv);
    if (!std::filesystem::exists(std::filesystem::path(path) / "js5-18.jcache"))
    {
        std::printf("SKIP: no npc cache at %s\n", path.c_str());
        return kExitSkip;
    }
    RSCache cache(path);
    std::printf("npc decode against %s\n", path.c_str());
    sweepAll(cache);
    checkHans(cache);
    std::printf("%s (%d failure(s))\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
