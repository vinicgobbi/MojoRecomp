// Host fibers: switching order, per-fiber MXCSR (the FIBER_FLAG_FLOAT_SWITCH
// guarantee guest code relies on), deep stacks, and use from several threads.
// Uses no guest code or game data.
#include "../host/fiber.h"

#include <xmmintrin.h>

#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using mojorecomp::host::FiberHandle;

int g_failures = 0;

void Check(bool condition, const char* description)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++g_failures;
}

constexpr unsigned kRoundingMask = _MM_ROUND_MASK;

struct Worker
{
    FiberHandle self = nullptr;
    FiberHandle root = nullptr;
    unsigned rounding = 0;
    int id = 0;
    std::vector<int>* trace = nullptr;
    bool roundingKept = true;
};

[[noreturn]] void WorkerMain(void* parameter)
{
    auto* worker = static_cast<Worker*>(parameter);
    _mm_setcsr((_mm_getcsr() & ~kRoundingMask) | worker->rounding);
    for (;;)
    {
        worker->trace->push_back(worker->id);
        mojorecomp::host::SwitchToFiber(worker->root);
        if ((_mm_getcsr() & kRoundingMask) != worker->rounding)
            worker->roundingKept = false;
    }
}

// Uses roughly `depth` KiB of stack.
uint64_t Recurse(int depth)
{
    volatile uint8_t frame[1024];
    frame[0] = static_cast<uint8_t>(depth);
    if (depth == 0)
        return frame[0];
    return frame[0] + Recurse(depth - 1);
}

struct DeepStack
{
    FiberHandle root = nullptr;
    uint64_t result = 0;
};

[[noreturn]] void DeepStackMain(void* parameter)
{
    auto* deep = static_cast<DeepStack*>(parameter);
    deep->result = Recurse(2048);  // about 2 MiB of the 4 MiB reservation
    for (;;)
        mojorecomp::host::SwitchToFiber(deep->root);
}

bool RunScenario()
{
    bool ok = true;
    bool converted = false;
    FiberHandle root = mojorecomp::host::ConvertThreadToFiber(converted);
    ok = root && converted && mojorecomp::host::CurrentFiber() == root && ok;

    const unsigned rootRounding = _MM_ROUND_TOWARD_ZERO;
    _mm_setcsr((_mm_getcsr() & ~kRoundingMask) | rootRounding);

    std::vector<int> trace;
    Worker workers[3];
    const unsigned roundings[3] = {_MM_ROUND_NEAREST, _MM_ROUND_UP, _MM_ROUND_DOWN};
    for (int i = 0; i < 3; ++i)
    {
        workers[i].root = root;
        workers[i].rounding = roundings[i];
        workers[i].id = i;
        workers[i].trace = &trace;
        workers[i].self = mojorecomp::host::CreateFiber(0x40000, 0x400000, WorkerMain,
                                                        &workers[i]);
        ok = workers[i].self != nullptr && ok;
    }
    if (!ok)
        return false;

    bool rootRoundingKept = true;
    for (int round = 0; round < 1000; ++round)
    {
        for (const Worker& worker : workers)
        {
            mojorecomp::host::SwitchToFiber(worker.self);
            if ((_mm_getcsr() & kRoundingMask) != rootRounding)
                rootRoundingKept = false;
        }
    }

    bool orderKept = trace.size() == 3000;
    for (size_t i = 0; orderKept && i < trace.size(); ++i)
        orderKept = trace[i] == int(i % 3);
    ok = orderKept && rootRoundingKept && ok;
    for (const Worker& worker : workers)
        ok = worker.roundingKept && ok;

    DeepStack deep;
    deep.root = root;
    FiberHandle deepFiber = mojorecomp::host::CreateFiber(0x40000, 0x400000, DeepStackMain, &deep);
    ok = deepFiber != nullptr && ok;
    if (deepFiber)
    {
        mojorecomp::host::SwitchToFiber(deepFiber);
        ok = deep.result != 0 && ok;
        mojorecomp::host::DeleteFiber(deepFiber);
    }

    for (const Worker& worker : workers)
        mojorecomp::host::DeleteFiber(worker.self);
    _mm_setcsr((_mm_getcsr() & ~kRoundingMask) | _MM_ROUND_NEAREST);
    ok = mojorecomp::host::ConvertFiberToThread() && ok;
    return ok;
}

} // namespace

int main()
{
    Check(RunScenario(), "fibers keep order, per-fiber MXCSR, and deep stacks on the main thread");
    Check(mojorecomp::host::CurrentFiber() == nullptr, "converted thread is no longer a fiber");

    bool workerThreadOk = false;
    std::thread thread([&workerThreadOk] { workerThreadOk = RunScenario(); });
    thread.join();
    Check(workerThreadOk, "fibers work the same on a second thread");

    Check(RunScenario(), "a thread can convert to fibers again after converting back");
    return g_failures == 0 ? 0 : 1;
}
