#include "fiber.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace mojorecomp::host {

FiberHandle ConvertThreadToFiber(bool& converted)
{
    converted = !IsThreadAFiber();
    return converted ? ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH)
                     : GetCurrentFiber();
}

bool ConvertFiberToThread()
{
    return ::ConvertFiberToThread() != FALSE;
}

FiberHandle CurrentFiber()
{
    return IsThreadAFiber() ? GetCurrentFiber() : nullptr;
}

FiberHandle CreateFiber(size_t committedStackBytes, size_t reservedStackBytes,
                        FiberEntry entry, void* parameter)
{
    return CreateFiberEx(committedStackBytes, reservedStackBytes, FIBER_FLAG_FLOAT_SWITCH,
                         reinterpret_cast<LPFIBER_START_ROUTINE>(entry), parameter);
}

void SwitchToFiber(FiberHandle fiber)
{
    ::SwitchToFiber(fiber);
}

void DeleteFiber(FiberHandle fiber)
{
    ::DeleteFiber(fiber);
}

} // namespace mojorecomp::host
