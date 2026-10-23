#include <algorithm>
#include <chrono>
#include <cstdio>

#include "../host/timing.h"

namespace {

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main()
{
    using namespace std::chrono_literals;
    using mojorecomp::host::PeriodicDeadline;

    PeriodicDeadline timer;
    const PeriodicDeadline::TimePoint zero{};

    if (timer.Active())
        return Fail("new periodic deadline should be inactive");

    timer.Reset(zero, 16ms);
    if (!timer.Active() || timer.Period() != 16ms || timer.Deadline() != zero + 16ms)
        return Fail("periodic deadline reset produced the wrong first deadline");

    timer.Advance();
    if (timer.Deadline() != zero + 32ms)
        return Fail("periodic deadline did not advance by one period");

    const uint64_t skipped = timer.SkipPast(zero + 81ms);
    if (skipped != 4 || timer.Deadline() != zero + 96ms)
        return Fail("periodic deadline catch-up arithmetic is incorrect");

    timer.RebaseDeadline(zero + 100ms);
    timer.Advance();
    if (timer.Deadline() != zero + 116ms)
        return Fail("periodic deadline rebase did not preserve its period");

    timer.Clear();
    if (timer.Active() || timer.SkipPast(zero + 1s) != 0)
        return Fail("cleared periodic deadline should stay inactive");

    std::puts("PASS: periodic deadline reset, advance, catch-up and rebase are deterministic.");

    // WaitUntil must never return before its deadline; an early return would
    // mean the host timer and steady_clock disagree. Lateness depends on the
    // scheduler, so it is reported rather than asserted.
    using mojorecomp::host::SteadyClock;
    constexpr int kWaits = 60;
    std::chrono::nanoseconds worstLate{0};
    std::chrono::nanoseconds totalLate{0};
    auto deadline = SteadyClock::now();
    for (int i = 0; i < kWaits; ++i)
    {
        deadline += 2ms;
        mojorecomp::host::WaitUntil(deadline);
        const auto late = SteadyClock::now() - deadline;
        if (late < std::chrono::nanoseconds::zero())
            return Fail("WaitUntil returned before its deadline");
        worstLate = std::max(worstLate, std::chrono::duration_cast<std::chrono::nanoseconds>(late));
        totalLate += std::chrono::duration_cast<std::chrono::nanoseconds>(late);
        deadline = std::max(deadline, SteadyClock::now());
    }
    std::printf("PASS: WaitUntil never returned early over %d waits "
                "(mean late %lld us, worst %lld us).\n",
                kWaits,
                static_cast<long long>(totalLate.count() / kWaits / 1000),
                static_cast<long long>(worstLate.count() / 1000));
    return 0;
}
