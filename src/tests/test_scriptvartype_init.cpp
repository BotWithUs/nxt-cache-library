// ScriptVarType first-use race test.
//
//   nxtcache-svt-init-test
//
// Releases many threads at once into the FIRST ScriptVarType lookup of the
// process, then checks every thread saw a fully built table. It is
// probabilistic by nature: a pass means the race did not show in this run, and
// the guarantee comes from the function-local static in ensureInitialized().
// It must be the first ScriptVarType use in the process, so it lives in its own
// executable and does nothing else first.
//
// Exit codes: 0 pass, 1 fail.

#include "config_types/ScriptVarType.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

namespace
{

constexpr int kThreads = 32;
constexpr int kLookupsPerThread = 2000;

struct Seen
{
    const ScriptVarType *intById{nullptr};
    const ScriptVarType *intByChar{nullptr};
    const ScriptVarType *stringById{nullptr};
    int misses{0};
};

// Every thread hammers lookups right after the gate opens; a thread that ran
// against a half-built map would miss an id or see a different pointer.
Seen lookUp(std::latch &gate)
{
    gate.arrive_and_wait();
    Seen seen;
    seen.intById = ScriptVarType::getScriptVarTypeById(0);
    seen.intByChar = ScriptVarType::getByChar(U'i');
    seen.stringById = ScriptVarType::getScriptVarTypeById(36);
    for (int i = 0; i < kLookupsPerThread; i++)
    {
        // INT (0) always exists, so any miss means the table was not ready.
        seen.misses += ScriptVarType::getScriptVarTypeById(0) == nullptr ? 1 : 0;
    }
    return seen;
}

}  // namespace

int main()
{
    std::latch gate(kThreads);
    std::array<Seen, kThreads> results{};
    {
        std::vector<std::jthread> threads;
        threads.reserve(kThreads);
        for (int t = 0; t < kThreads; t++)
        {
            threads.emplace_back([&gate, &results, t]() { results[t] = lookUp(gate); });
        }
    }

    const ScriptVarType *expectedInt = ScriptVarType::getScriptVarTypeById(0);
    const ScriptVarType *expectedString = ScriptVarType::getScriptVarTypeById(36);
    int bad = 0;
    for (const Seen &seen : results)
    {
        const bool isOk = seen.intById != nullptr && seen.intById == expectedInt &&
                          seen.intByChar == expectedInt && seen.stringById == expectedString &&
                          seen.misses == 0;
        bad += isOk ? 0 : 1;
    }
    const bool isTableSane = expectedInt != nullptr && expectedInt->getName() == "INT" &&
                             expectedString != nullptr && expectedString->getName() == "STRING";
    std::printf("  %s  %d threads saw the same, complete table on first use (bad=%d)\n",
                bad == 0 ? "ok  " : "FAIL", kThreads, bad);
    std::printf("  %s  table contents: INT(0) and STRING(36) resolve by id\n", isTableSane ? "ok  " : "FAIL");
    const bool isPass = bad == 0 && isTableSane;
    std::printf("\n%s\n", isPass ? "PASS" : "FAIL");
    return isPass ? 0 : 1;
}
