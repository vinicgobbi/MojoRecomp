#include "fiber.h"

#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

namespace mojorecomp::host {
namespace {

struct PosixFiber
{
    ucontext_t context{};
    void* stack = nullptr;      // includes the guard page; null for a converted thread
    size_t stackBytes = 0;
    FiberEntry entry = nullptr;
    void* parameter = nullptr;
};

thread_local PosixFiber* t_currentFiber = nullptr;
thread_local PosixFiber* t_threadFiber = nullptr;

// makecontext cannot portably pass a pointer, so a new fiber finds itself
// through the thread's current fiber, which SwitchToFiber sets before
// switching.
void FiberTrampoline()
{
    PosixFiber* self = t_currentFiber;
    self->entry(self->parameter);
    std::fputs("[host fiber] fiber entry returned\n", stderr);
    std::abort();
}

size_t PageSize()
{
    static const size_t size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    return size;
}

} // namespace

FiberHandle ConvertThreadToFiber(bool& converted)
{
    if (t_currentFiber)
    {
        converted = false;
        return t_currentFiber;
    }
    converted = true;
    t_threadFiber = new PosixFiber();
    t_currentFiber = t_threadFiber;
    return t_threadFiber;
}

bool ConvertFiberToThread()
{
    if (!t_threadFiber || t_currentFiber != t_threadFiber)
        return false;
    delete t_threadFiber;
    t_threadFiber = nullptr;
    t_currentFiber = nullptr;
    return true;
}

FiberHandle CurrentFiber()
{
    return t_currentFiber;
}

FiberHandle CreateFiber(size_t committedStackBytes, size_t reservedStackBytes,
                        FiberEntry entry, void* parameter)
{
    // Linux commits stack pages lazily, so only the reservation matters.
    (void)committedStackBytes;
    if (!entry || !reservedStackBytes)
        return nullptr;

    const size_t page = PageSize();
    const size_t usable = (reservedStackBytes + page - 1) / page * page;
    const size_t total = usable + page;
    void* stack = mmap(nullptr, total, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED)
        return nullptr;
    // The lowest page stays inaccessible so an overflow faults instead of
    // corrupting a neighbouring mapping.
    if (mprotect(stack, page, PROT_NONE) != 0)
    {
        munmap(stack, total);
        return nullptr;
    }

    auto* fiber = new PosixFiber();
    fiber->stack = stack;
    fiber->stackBytes = total;
    fiber->entry = entry;
    fiber->parameter = parameter;
    if (getcontext(&fiber->context) != 0)
    {
        munmap(stack, total);
        delete fiber;
        return nullptr;
    }
    fiber->context.uc_stack.ss_sp = static_cast<char*>(stack) + page;
    fiber->context.uc_stack.ss_size = usable;
    fiber->context.uc_link = nullptr;
    makecontext(&fiber->context, FiberTrampoline, 0);
    return fiber;
}

void SwitchToFiber(FiberHandle fiber)
{
    auto* target = static_cast<PosixFiber*>(fiber);
    PosixFiber* source = t_currentFiber;
    if (!source || !target || source == target)
    {
        std::fputs("[host fiber] invalid fiber switch\n", stderr);
        std::abort();
    }
    t_currentFiber = target;
    // swapcontext saves and restores MXCSR and the x87 control word along with
    // the integer registers, matching FIBER_FLAG_FLOAT_SWITCH on Windows.
    if (swapcontext(&source->context, &target->context) != 0)
    {
        std::fputs("[host fiber] swapcontext failed\n", stderr);
        std::abort();
    }
}

void DeleteFiber(FiberHandle fiber)
{
    auto* target = static_cast<PosixFiber*>(fiber);
    if (!target || target == t_currentFiber)
        return;
    if (target->stack)
        munmap(target->stack, target->stackBytes);
    delete target;
}

} // namespace mojorecomp::host
