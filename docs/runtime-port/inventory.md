# Runtime Port Inventory

Survey of what ties the native runtime to Windows. It is the factual baseline
for the work packages in [README.md](README.md). Update a row when a package
changes it; do not let this file drift into a history log.

Surveyed at commit `8b1106279e5d` (2026-10-22): 114 source files under
`runtime/`, about 42,100 lines, excluding generated PPC code and build trees.
The pinned submodules (XenonRecomp, XenosRecomp, FFmpeg, SDL, Vulkan-Headers,
extract-xiso) were not initialized in the surveying checkout; items that depend
on their contents are listed under [Still to verify](#still-to-verify).

## Summary

| Area | Windows dependency | Linux replacement | Size |
| --- | --- | --- | --- |
| Build and toolchain | clang-cl, MSVC CRT, Visual Studio env, Windows-only CMake targets | clang + libstdc++/libc++, CMake presets | Large |
| Setup and tools | `.bat` entry points, hard-coded install paths, `.exe` tool names | Python `tools/` entry points and CMake | Medium |
| Window | Win32 window class and message loop | SDL3 window | Medium |
| Vulkan surface and loader | `vkCreateWin32SurfaceKHR`, `LoadLibraryW("vulkan-1.dll")` | `SDL_Vulkan_CreateSurface`, `libvulkan.so.1` | Small |
| Guest fibers | `ConvertThreadToFiberEx`, `SwitchToFiber` | Context switch with explicit FP state | Large, high risk |
| Audio output | `waveOut*` | SDL3 audio stream | Medium |
| Keyboard | `GetAsyncKeyState` for debug and F12 keys | SDL3 keyboard state | Small |
| Gamepad | XInput DLL plus SDL3 | SDL3 only on Linux | Small |
| On-screen text | GDI font rasterization | Open decision (see README) | Medium |
| Guest memory write-watch | Vectored exception handler + `VirtualProtect` | `SIGSEGV` handler + `mprotect` | Medium, high risk |
| Timing | Waitable timers | `clock_nanosleep` | Small |
| Crash reporting | SEH, `MiniDumpWriteDump` | Signal handler with text log and JSON (RP-09) | Medium |
| Threads | `SetThreadDescription`, `SetThreadPriority`, `GetCurrentThreadId` | `pthread_setname_np`, scheduling hints, `gettid` | Small |
| Dynamic libraries | `LoadLibrary`/`GetProcAddress` | `dlopen`/`dlsym` | Small |
| LGPL and tool binaries | `mojorecomp-ffmpeg.dll`, `mojorecomp-lzx.dll`, `extract-xiso.exe` | `.so` with `$ORIGIN` rpath, ELF tool | Medium |
| Shader compiler | Prebuilt Windows `dxcompiler.dll`, `dxil.dll`, `dxc.exe` | `libdxcompiler.so` | Medium, provenance needed |

Already portable and only needing verification: the guest address space
(`kernel/memory.cpp` already has an `mmap` path), case-insensitive guest file
lookup (`kernel/vfs.cpp`), 64-bit file seek and resize (`kernel/imports.cpp`,
`title_resources.cpp` already have POSIX branches), and the DXC loader
(`gpu/shader_translator.cpp` already names `libdxcompiler.so` and uses `dlopen`
off Windows).

## Source files

Counts are matches of Win32 identifiers per file. False positives were removed:
`VK_*` and `VK_NULL_HANDLE` in the Vulkan code, and the guest XInput and kernel
hooks (`*_x`, `XInputState`, ...) in `kernel/imports.cpp`, which emulate the
Xbox API and are platform-neutral.

### Host platform layer

| File | Lines | Windows usage | Planned replacement |
| --- | ---: | --- | --- |
| `host/window.cpp`, `host/window.h` | 314 | Window class, `WndProc`, `SetWindowPos`, monitor queries, message pump, `WM_APP` title updates | SDL3 window and event loop (RP-11) |
| `host/input.cpp` | 929 | XInput loaded with `LoadLibraryW`; SDL3 gamepads behind `MOJORECOMP_HAS_SDL3` | SDL3 only on Linux, keep merge logic (RP-13) |
| `host/timing.cpp` | 99 | High-resolution waitable timer | Linux uses `clock_nanosleep` with `CLOCK_MONOTONIC` (RP-06) |
| `host/host_paths.cpp` | 26 | `GetModuleFileNameW`; falls back to the current directory | Resolved by RP-05: `host::ExecutablePath()` (`/proc/self/exe` on Linux) |
| `host/system_font_atlas.cpp` | 94 | GDI: `CreateFontW`, `CreateDIBSection`, `TextOutW` | Open decision D2 (RP-15) |
| `host/crash_reporter_windows.cpp` | 525 | SEH (`__try`/`__except`), `SetUnhandledExceptionFilter`, `MiniDumpWriteDump` | Resolved by RP-09: `host/crash_reporter_posix.cpp` (signal handler, text log and JSON, decision D4); stays Windows-only |
| `debug_mode.cpp` | 395 | `GetAsyncKeyState(VK_F1..VK_F10)`, `GetForegroundWindow` | SDL3 keyboard state and window focus (RP-13) |
| `boot_probe.cpp` | 1024 | Runtime entry point: window placement, `GetModuleFileNameW`, process waits, `EXCEPTION_ACCESS_VIOLATION` reporting | Split host-specific parts into the host layer (RP-19) |

### CPU and kernel

| File | Lines | Windows usage | Planned replacement |
| --- | ---: | --- | --- |
| `cpu/guest_fiber.cpp` | 152 | None | Resolved by RP-08: `host/fiber.h` (Win32 fibers on Windows, `ucontext` on Linux, decision D1) |
| `cpu/timebase.cpp` | 145 | One `_MSC_VER` branch, in the ARM64 path only | None: x86-64 already uses `__builtin_ia32_rdtsc` and ARM64 is out of scope |
| `kernel/memory.cpp` | 154 | None | Resolved by RP-07: reservation moved to `host/guest_address_space_*.cpp` and covered by `GuestMemorySnapshot` |
| `kernel/imports.cpp` | 4752 | None; compiles on Linux | Resolved by RP-05 and RP-06: host thread id, file helpers, and `host::RecursiveLock`; calendar conversion in `kernel/time_fields.h` |
| `title_resources.cpp` | 39 | `_fseeki64`/`_ftelli64` | Resolved by RP-05: host 64-bit file helpers |

### GPU

| File | Lines | Windows usage | Planned replacement |
| --- | ---: | --- | --- |
| `gpu/vk_presenter.cpp` | 14897 | `vkCreateWin32SurfaceKHR`, loads `vulkan-1.dll`, `HWND`, `GetAsyncKeyState(VK_F12)`, BMP structs for screenshots | SDL3 surface and Vulkan loader (RP-12); thread id and priority moved to the host layer (RP-05) |
| `gpu/hardware_probe.cpp` | 330 | Win32 surface and loader for the launcher hardware probe | Same as above (RP-12) |
| `gpu/shader_translator.cpp` | 1399 | COM base headers for `dxcapi.h` | DXC loading moved to `host::DynamicLibrary` (RP-05); compiles on Linux with `__EMULATE_UUID`; verify with a Linux `libdxcompiler.so` (RP-17) |
| `gpu/guest_memory_snapshot.cpp` | 561 | None | Resolved by RP-07: host `SetPageAccess` and write-fault handler (`SIGSEGV` + `mprotect` on Linux) |
| `gpu/gpu_executor.cpp`, `gpu/vd.cpp` | 1244 | None | Resolved by RP-05: host thread naming and id; both compile on Linux |

### Audio

| File | Lines | Windows usage | Planned replacement |
| --- | ---: | --- | --- |
| `audio/xaudio.cpp` | 1204 | `waveOut*` device, Win32 events | SDL3 audio stream (RP-14) |
| `audio/xma_decoder.cpp` | 1284 | None (`DWORD` appears only in comments about XMA context words) | None |

### Tests

`tests/window_mode_test.cpp`, `tests/texture_gpu_probe.cpp`,
`tests/event_probe.cpp`, and `tests/input_probe.cpp` use Win32 directly or
link Windows libraries. They move with the code they test.
`tests/guest_memory_snapshot_test.cpp` runs its write-watch checks on both
hosts through the host layer (RP-07). `tests/pm4_extent_probe.cpp` reserves
its guest space with `mmap` on Linux (RP-01).

## Build system

- `runtime/CMakeLists.txt` (657 lines) only defines the runtime executable
  (`MojoRecompBootProbe`, shipped as `cot-runtime.exe`) inside `if(WIN32)`. On
  other hosts only the unit tests and `MojoRecompSmoke` would be defined.
- Configuration stops with a fatal error when `ppc/ppc_func_mapping.cpp` is
  missing. `-DMOJORECOMP_GUEST_IMAGE=OFF` configures only the
  game-independent tests, which `runtime-tests.yml` runs in CI on both hosts
  (RP-01). They need only the XenonRecomp submodule for tomlplusplus.
- MSVC-ABI assumptions: static MSVC runtime, `/EHsc`, `/O2`, `/WHOLEARCHIVE`,
  `/MAP`, `/FI`, and `.rc` resources. CRT, exception, optimization,
  forced-include, and whole-archive flags are selected by compiler family.
  The remaining MSVC-only flags belong to the Windows runtime executable,
  which RP-19 mirrors for Linux.
- XenonUtils `fmt` and `xxhash` archives are located by the platform's
  static-library prefix and suffix, and `clang_rt.builtins` is located in the
  active clang-cl's resource directory instead of a fixed `llvm22` path
  (RP-02). Thin LTO links through `lld` with the GNU-driver Clang.
- `runtime/CMakePresets.json` defines `windows-clang-cl` and `linux-clang`
  presets plus `-tests` variants that configure without a guest image.
- Windows link libraries: `onecore`, `winmm`, `dbghelp`, `user32`, `gdi32`,
  `imm32`, `ole32`, `oleaut32`, `version`, `uuid`, `advapi32`, `setupapi`,
  `shell32`, `hid`, `dinput8`.
- SDL3 is only used when `thirdparty/build/SDL/SDL3-static.lib` exists.
- The FFmpeg and LZX libraries are looked up as `libavcodec.lib` plus
  `mojorecomp-ffmpeg.dll`; DXC as `thirdparty/dxc-bin/bin/x64/dxc.exe`.
- Generated PPC code is compiled with `-msse4.1 -mavx` and thin LTO under
  Clang; both are toolchain-neutral.

## Setup and tools

| Entry point | Role | Windows assumptions |
| --- | --- | --- |
| `setup.bat` -> `tools/setup_toolchain.py` | Checks submodules, copies them to `thirdparty/work`, applies patches, builds XenonRecomp, XenosRecomp, FFmpeg, SDL3, extract-xiso | Resolved by RP-03: Visual Studio located with `vswhere`, CMake and Ninja from `PATH`, `thirdparty/llvm22` overridable with `--llvm-root`; FFmpeg skipped on Linux until RP-16 |
| `tools/apply-*-patches.bat` -> `tools/setup_toolchain.py apply-patch` | `git apply` of `patches/*.patch` into the work trees | None beyond batch syntax |
| `tools/analyze.bat` -> `tools/analyze.py` | Runs `XenonAnalyse` on `game/default.xex` | Resolved by RP-04: executable suffix chosen per platform |
| `tools/recompile.bat` -> `tools/recompile.py` | Runs `XenonRecomp`, writes `ppc/` | Resolved by RP-04: executable suffix chosen per platform |
| `build-smoke.bat` | Configures and builds the runtime with clang-cl, runs `MojoRecompSmoke.exe` | Visual Studio and CMake install paths |
| `release.bat` | COT runtime release candidate | `tar.exe`, `npm.cmd`, Windows runtime artifacts |

The maintained patches contain no Windows-specific code: the XenonRecomp patch
touches the recompiler and `XenonUtils/ppc_context.h`, the XenosRecomp patch
the shader recompiler, and the extract-xiso patch removes FTP support.

## Third-party binaries

| Component | Windows artifact | Linux artifact | Notes |
| --- | --- | --- | --- |
| FFmpeg (LGPL, replaceable) | `mojorecomp-ffmpeg.dll` + import lib | `libmojorecomp-ffmpeg.so` | Built by `tools/ffmpeg-rexglue`; keep it dynamically linked and replaceable |
| libmspack LZX (LGPL, replaceable) | `mojorecomp-lzx.dll` | `libmojorecomp-lzx.so` | Built by `tools/libmspack-lzx` |
| DXC | `dxcompiler.dll`, `dxil.dll` (prebuilt, from XenosRecomp `dxc-bin`) | `libdxcompiler.so`, `libdxil.so` | The pinned `dxc-bin` already ships Linux x64 `libdxcompiler.so`, `libdxil.so`, and `dxc-linux` built from the same DXC commit as the Windows DLLs (`416fab6b`, 1.8.2407); license provenance still to record (D3) |
| SDL3 | `SDL3-static.lib` | static `libSDL3.a` | Same pinned submodule |
| extract-xiso | `extract-xiso.exe` | `extract-xiso` | Built on Linux by `tools/extract-xiso` (RP-18) |
| Vulkan loader | system `vulkan-1.dll` | system `libvulkan.so.1` | Not shipped |

The launcher already expects the Linux runtime component to contain
`cot-runtime`, `libdxcompiler.so`, `libmojorecomp-ffmpeg.so`,
`libmojorecomp-lzx.so`, and `extract-xiso`
(`launcher/src-tauri/src/platform.rs`). That list is provisional and must be
confirmed by RP-20.

## Still to verify

Verified since the survey: XenonRecomp, XenonAnalyse, XenosRecomp, SDL3, and
extract-xiso build with Clang on Linux with the maintained patches (RP-03,
RP-18); the generated PPC code compiles with Linux Clang (RP-04); and the
pinned `dxc-bin` ships matching Linux binaries (see the table above).

These need the pinned submodules or the game and could not be checked in the
survey:

- `tools/ffmpeg-rexglue` and `tools/libmspack-lzx` build shared libraries on
  Linux with the same exported surface.
- `MojoRecompSmoke` builds on Linux. (The 16 game-independent tests build
  and pass on Linux with GCC; Clang on Linux is covered by CI.)
