# Native Runtime Port

Plan for making the COT runtime build and run natively on Windows x64 and
Linux x64. The factual baseline (what depends on Windows today, file by file)
is in [inventory.md](inventory.md).

Status: **in progress**. RP-01 to RP-06 and RP-18 are in progress. The launcher already runs
natively on Linux and expects a `platform = "linux"` runtime component.

## Goal and scope

- One runtime source tree that builds natively on both hosts, with Windows
  remaining the established and continuously working target.
- Linux uses the same Vulkan renderer, guest kernel, and recompiled guest code.
  Only the host layer differs.
- Out of scope: Wine, Proton, or any compatibility-layer bridge; rewriting the
  runtime in another language; macOS; ARM64; MOM.

## Working rules

- **Windows never regresses.** Every package keeps the Windows build, its
  tests, and its behavior intact. Linux-only progress that breaks Windows is
  not done.
- **Small, independent packages.** Each package below is meant to be one
  reviewable change (or a short series) with its own validation. Packages may
  land on Windows before Linux can use them.
- **Host abstraction first.** Shared code calls the host layer
  (`runtime/host/`); `#ifdef _WIN32` belongs inside that layer, not in guest,
  kernel, or GPU code. A platform branch without its counterpart must fail
  safely with a clear "unsupported" error.
- **SDL3 for window, input, and audio** on both hosts, since it is already a
  pinned dependency, instead of a second hand-written Linux backend.
- **Evidence over assumption.** Packages that touch fibers, memory protection,
  timing, or presentation need a probe or test, and gameplay or visual changes
  need manual validation on representative scenes (AGENTS.md).
- **Licensing travels with binaries.** Any new shipped library (`.so`, DXC,
  fonts) needs its license text, provenance, and notice updates in the same
  package.

## Work packages

Status values: `todo`, `in progress`, `done`, `blocked`. Update the table and
the package section when a package changes state, and update
[inventory.md](inventory.md) rows it resolves.

| ID | Package | Phase | Depends on | Status |
| --- | --- | --- | --- | --- |
| RP-01 | Configure and test the runtime without a guest image | 0 Foundations | - | in progress |
| RP-02 | Portable CMake toolchain and presets | 0 Foundations | RP-01 | in progress |
| RP-03 | Cross-platform setup entry point (replaces `setup.bat`) | 0 Foundations | RP-02 | in progress |
| RP-04 | Cross-platform analyze and recompile entry points | 0 Foundations | RP-03 | in progress |
| RP-05 | Host platform primitives (paths, threads, libraries) | 1 Host layer | RP-01 | in progress |
| RP-06 | Host synchronization and timing | 1 Host layer | RP-05 | in progress |
| RP-07 | Guest memory mapping and write-watch | 1 Host layer | RP-05 | todo |
| RP-08 | Portable guest fibers | 1 Host layer | RP-05 | todo |
| RP-09 | Crash reporting on Linux | 1 Host layer | RP-05 | todo |
| RP-10 | SDL3 as a required dependency on both hosts | 2 Platform services | RP-03 | todo |
| RP-11 | SDL3 window | 2 Platform services | RP-10 | todo |
| RP-12 | Vulkan surface and loader through SDL3 | 2 Platform services | RP-11 | todo |
| RP-13 | Keyboard and gamepad input through SDL3 | 2 Platform services | RP-11 | todo |
| RP-14 | SDL3 audio output | 2 Platform services | RP-10 | todo |
| RP-15 | Portable on-screen text atlas | 2 Platform services | RP-05, D2 | todo |
| RP-16 | FFmpeg and LZX shared libraries on Linux | 3 Native libraries | RP-03 | todo |
| RP-17 | DXC on Linux | 3 Native libraries | RP-03, D3 | todo |
| RP-18 | extract-xiso on Linux | 3 Native libraries | RP-03 | in progress |
| RP-19 | Linux runtime executable and smoke build | 4 Integration | RP-04, RP-06 to RP-18 | todo |
| RP-20 | Linux runtime release package | 4 Integration | RP-19 | todo |
| RP-21 | Launcher and runtime integration on Linux | 4 Integration | RP-20 | todo |
| RP-22 | Manual gameplay and visual validation | 4 Integration | RP-21 | todo |

Phase 0 and phase 1 packages are valuable on Windows alone (CI coverage,
cleaner host layer), so they can land before any Linux runtime exists.

### Phase 0: Foundations

**RP-01 Configure and test the runtime without a guest image.**
Configuration currently stops when `ppc/ppc_func_mapping.cpp` is missing, so no
unit test can run in CI. Add an option (for example
`MOJORECOMP_GUEST_IMAGE=OFF`) that configures only game-independent targets
and tests. Add a CI job running those tests on Windows and Linux.
Done when: CTest runs the game-independent tests on both hosts in CI without
`ppc/`, and the local full build is unchanged.

**RP-02 Portable CMake toolchain and presets.**
Remove machine-specific paths and MSVC-only assumptions from
`runtime/CMakeLists.txt`: select CRT, exception, optimization, whole-archive,
forced-include, and builtins flags by compiler family; discover XenonUtils
libraries by target name or platform suffix; add `CMakePresets.json` with
`windows-clang-cl` and `linux-clang` presets.
Done when: the Windows preset builds a runtime that behaves exactly like
today's build, and the Linux preset configures the RP-01 targets.

**RP-03 Cross-platform setup entry point.**
Move `setup.bat` into a Python program under `tools/` (submodule checks, work
tree copies, patch application, dependency builds) per the tooling policy.
`setup.bat` then only forwards to it. The patch helpers
(`tools/apply-*-patches.bat`) move into the same program.
Done when: the new entry point reproduces `setup.bat` on Windows and builds
XenonRecomp, XenosRecomp, SDL3, and extract-xiso on Linux. The FFmpeg recipe
is still Windows-only; the entry point skips it on Linux with a message until
RP-16 ports it. The Linux SDL3 build needs SDL's
system build dependencies (X11, Wayland, audio, udev development packages).

**RP-04 Cross-platform analyze and recompile entry points.**
Make `tools/recompile.py` find tools by platform executable name and add a
Python replacement for `tools/analyze.bat`. Verify that XenonRecomp output and
the generated PPC code compile with Linux Clang.
Done when: the owner can produce `ppc/` and the switch tables on Linux from
their own game files, and the generated code compiles there.
Linux status: `tools/analyze.py` reproduces the committed switch tables
byte for byte (380 tables), `tools/recompile.py` generates `ppc/`, and all 79
generated translation units compile with Clang 21 using the runtime's flags
and no diagnostics. Remaining: run both through the forwarding batch files on
Windows.

### Phase 1: Host layer

**RP-05 Host platform primitives.**
Introduce small host APIs for executable path, current thread id, thread name,
thread priority hint, dynamic library loading, file sync, and fixed-width
types, then route the existing call sites through them (`host_paths.cpp`,
`gpu_executor.cpp`, `vd.cpp`, `vk_presenter.cpp`, `imports.cpp`,
`timebase.cpp`, `xma_decoder.cpp`).
Done when: no shared file outside `runtime/host/` calls these Win32 APIs and
Windows behavior is unchanged.
Status: `host/platform.h` with `platform_windows.cpp` and `platform_posix.cpp`
(selected by CMake) covers executable path, thread id, name, and priority,
64-bit file positioning, resizing and sync, shared library names, and dynamic
loading, with a `HostPlatform` test. All listed call sites plus
`title_resources.cpp` use it; `timebase.cpp` and `xma_decoder.cpp` needed no
change. The Vulkan loader stays with RP-12, where SDL3 loads it, and the
remaining `boot_probe.cpp` calls with RP-19. Remaining: build and run the
runtime on Windows.

**RP-06 Host synchronization and timing.**
Replace the waitable timer in `host/timing.cpp` with a host timer using
`clock_nanosleep` on Linux, and decide whether the native `CRITICAL_SECTION`
path in `kernel/imports.cpp` stays as a Windows optimization or the existing
`std::recursive_mutex` path becomes the default (measure stutter with the
stutter profiler before deciding).
Done when: `MojoRecompTimingTest` passes on both hosts and frame pacing at
30 FPS is unchanged on Windows.
Status: on Linux `WaitUntil` sleeps with `clock_nanosleep(CLOCK_MONOTONIC,
TIMER_ABSTIME)` instead of sleeping and then yielding in a loop for the last
millisecond. Measured at a 33.3 ms period: mean lateness 137 us (was 260 us),
p99 1.4 ms (was 4.7 ms), CPU 0.1% (was 2.3%); at a 2 ms period the old loop
burned 42% of a core. `HostTiming` now also checks that `WaitUntil` never
returns early. Guest critical sections use `host::RecursiveLock`: Windows keeps
the native `CRITICAL_SECTION` path and `MOJORECOMP_CS_NATIVE` unchanged, and
Linux uses `std::recursive_mutex`, so the Windows default decision is deferred
until it can be measured in game. `RtlTimeToTimeFields` and
`RtlTimeFieldsToTime` use host-independent calendar arithmetic
(`kernel/time_fields.h`, `TimeFields` test, compared against the Win32 API on
Windows), which removes `windows.h` from `kernel/imports.cpp`; it now compiles
on Linux. Remaining: the Windows build and a 30 FPS pacing check in game.

**RP-07 Guest memory mapping and write-watch.**
Test the existing `mmap` path in `kernel/memory.cpp` and port the write
tracking in `gpu/guest_memory_snapshot.cpp` from a vectored exception handler
with `VirtualProtect` to a `SIGSEGV` handler with `mprotect`, keeping the
handler async-signal-safe.
Done when: `GuestMemorySnapshot` and a mapping probe pass on both hosts.

**RP-08 Portable guest fibers.**
Replace `ConvertThreadToFiberEx`/`SwitchToFiber` in `cpu/guest_fiber.cpp` with
a context switch that also preserves FP and SSE control state (the Windows
code relies on `FIBER_FLAG_FLOAT_SWITCH`), and keep the generated code's FPSCR
resynchronization. Candidates: `ucontext`, or a small assembly switch shared by
both hosts. This is the highest-risk package.
Done when: `FiberProbe` passes on both hosts and fiber-heavy scenes behave the
same on Windows.

**RP-09 Crash reporting on Linux.**
Ship `host/crash_reporter_stub.cpp` on Linux first, then add a signal-based
reporter that writes the same support information the launcher collects
(minidump equivalent to be decided).
Done when: a Linux crash leaves a diagnosable record the support package can
include.

### Phase 2: Platform services

**RP-10 SDL3 as a required dependency.**
Build static SDL3 on both hosts and make it required instead of optional
(`MOJORECOMP_HAS_SDL3`). SDL silently drops backends whose development
packages are missing (for example it configures with no ALSA, PulseAudio, or
PipeWire audio), so the Linux build must require the audio, Wayland, X11, and
udev backends the runtime relies on instead of accepting SDL's defaults.

**RP-11 SDL3 window.**
Replace the Win32 window in `host/window.cpp` and window handling in
`boot_probe.cpp` with SDL3, preserving windowed, borderless, and fullscreen
modes and title updates.
Done when: `window_mode_test` (ported) passes and Windows window behavior is
unchanged in manual review.

**RP-12 Vulkan surface and loader.**
Create the surface with `SDL_Vulkan_CreateSurface` and load the Vulkan loader
by platform name in `gpu/vk_presenter.cpp` and `gpu/hardware_probe.cpp`.
Keep adapter selection independent of vendor names.
Done when: the hardware probe reports correctly on both hosts.

**RP-13 Keyboard and gamepad input.**
Move debug hotkeys (`debug_mode.cpp`) and the F12 key (`vk_presenter.cpp`) to
SDL3 keyboard state with window focus; use SDL3 gamepads on Linux while
keeping the existing XInput and SDL merge on Windows until parity is proven.

**RP-14 SDL3 audio output.**
Replace `waveOut*` in `audio/xaudio.cpp` with an SDL3 audio stream, keeping
the guest-facing XAudio timing contract.
Done when: audio latency and sync match Windows in manual review.

**RP-15 Portable on-screen text atlas.**
Replace GDI rasterization in `host/system_font_atlas.cpp` according to
decision D2.

### Phase 3: Native libraries

**RP-16 FFmpeg and LZX shared libraries.**
Build `libmojorecomp-ffmpeg.so` and `libmojorecomp-lzx.so` from the same
recipes, loaded through `$ORIGIN` so users can still replace them (LGPL), and
publish their corresponding sources as on Windows.

**RP-17 DXC on Linux.**
Ship a `libdxcompiler.so` that matches the Windows DXC behavior, with license
and provenance recorded (decision D3). The loader in
`gpu/shader_translator.cpp` already supports it.

**RP-18 extract-xiso on Linux.**
Build the patched extract-xiso as an ELF tool for the runtime component.
Linux status: the recipe builds upstream's `__LINUX__` configuration and the
result lists and fully extracts the supported retail ISO. Remaining: confirm
the Windows build is unchanged, and ship the tool in the Linux runtime
package (RP-20).

### Phase 4: Integration

**RP-19 Linux runtime executable and smoke build.**
Define the runtime executable and `MojoRecompSmoke` for Linux, replace
`build-smoke.bat` with a cross-platform entry point, and set the runtime's
library search path to its own directory.

**RP-20 Linux runtime release package.**
Make the runtime release packaging cross-platform, finalize the Linux file
list in `launcher/src-tauri/src/platform.rs`, and publish `platform = "linux"`
runtime entries in `update-catalog-linux-x64.toml`.

**RP-21 Launcher and runtime integration on Linux.**
Verify install, launch, environment variables (`MOJORECOMP_DXC_LIB`, data
roots), process lifetime, presence, and support packages end to end.

**RP-22 Manual gameplay and visual validation.**
Representative scenes on Mesa (RADV, ANV) and NVIDIA on Linux, plus a Windows
regression pass. Linux must not be described as supported until this passes.

## Open decisions

| ID | Question | Options | Needed by |
| --- | --- | --- | --- |
| D1 | Fiber implementation | `ucontext` (simple, slower switches) or a shared assembly switch for both hosts | RP-08 |
| D2 | On-screen text without GDI | Bundled font with stb_truetype (needs font license review), or system fonts through fontconfig and FreeType | RP-15 |
| D3 | DXC for Linux | Official DXC Linux release matching the Windows binaries, or building DXC from source | RP-17 |
| D4 | Linux crash dumps | Signal handler with backtrace text, or a minidump library | RP-09 |
| D5 | Minimum Linux baseline | glibc version and distributions to support, matching the launcher AppImage | RP-19 |

Record each decision here, with the reason, when it is made.
