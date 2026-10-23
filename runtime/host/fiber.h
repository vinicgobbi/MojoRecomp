#pragma once

// Cooperative host fibers backing guest fibers. Windows uses native Win32
// fibers with FIBER_FLAG_FLOAT_SWITCH; Linux uses ucontext, whose context
// switch also saves and restores MXCSR and the x87 control word. One
// implementation per host, selected by CMake.

#include <cstddef>

namespace mojorecomp::host {

using FiberHandle = void*;
using FiberEntry = void (*)(void* parameter);

// Lets the calling thread run fibers and returns the fiber for its current
// stack. `converted` reports whether this call did the conversion (false if
// the thread already was a fiber). Returns nullptr on failure.
FiberHandle ConvertThreadToFiber(bool& converted);

// Undoes a conversion made by ConvertThreadToFiber on this thread.
bool ConvertFiberToThread();

// The fiber running on the calling thread, or nullptr if it is not a fiber.
FiberHandle CurrentFiber();

// Creates a fiber that runs `entry(parameter)` on its first switch. The entry
// must never return. Returns nullptr on failure.
FiberHandle CreateFiber(size_t committedStackBytes, size_t reservedStackBytes,
                        FiberEntry entry, void* parameter);

// Switches the calling thread to `fiber`. Must not target the current fiber.
void SwitchToFiber(FiberHandle fiber);

// Frees a fiber that is not running on any thread.
void DeleteFiber(FiberHandle fiber);

} // namespace mojorecomp::host
