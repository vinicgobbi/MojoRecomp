// Host platform primitives: executable path, thread identity and naming,
// 64-bit file helpers, and dynamic library loading. Uses only a temporary
// file and a library that every supported host provides.
#include "../host/platform.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>

namespace {

int g_failures = 0;

void Check(bool condition, const char* description)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++g_failures;
}

#if defined(_WIN32)
constexpr const char* kSystemLibrary = "kernel32.dll";
constexpr const char* kSystemSymbol = "GetCurrentThreadId";
constexpr const char* kExpectedDxcName = "dxcompiler.dll";
#else
constexpr const char* kSystemLibrary = "libc.so.6";
constexpr const char* kSystemSymbol = "getpid";
constexpr const char* kExpectedDxcName = "libdxcompiler.so";
#endif

void TestExecutablePath()
{
    const auto path = mojorecomp::host::ExecutablePath();
    std::error_code error;
    Check(!path.empty() && std::filesystem::is_regular_file(path, error),
          "ExecutablePath names an existing file");
    Check(path.stem().string().find("HostPlatformTest") != std::string::npos,
          "ExecutablePath names this test executable");
}

void TestThreads()
{
    const uint32_t mainId = mojorecomp::host::CurrentThreadId();
    uint32_t workerId = 0;
    std::thread worker([&workerId] {
        mojorecomp::host::SetCurrentThreadName("MojoRecomp host platform test worker");
        mojorecomp::host::SetCurrentThreadPriority(
            mojorecomp::host::ThreadPriority::BelowNormal);
        workerId = mojorecomp::host::CurrentThreadId();
    });
    worker.join();
    Check(mainId != 0 && workerId != 0, "CurrentThreadId is nonzero");
    Check(mainId != workerId, "CurrentThreadId differs between threads");
    Check(mainId == mojorecomp::host::CurrentThreadId(), "CurrentThreadId is stable");
}

void TestFiles()
{
    const auto path = std::filesystem::temp_directory_path() /
        ("mojorecomp-host-platform-" +
         std::to_string(mojorecomp::host::CurrentThreadId()) + ".bin");
    std::FILE* file = std::fopen(path.string().c_str(), "w+b");
    Check(file != nullptr, "temporary file opens");
    if (!file)
        return;

    // Seeking past the end allocates nothing, so a position beyond 4 GiB covers
    // 64-bit offsets without writing data. (_chsize_s writes real zeros.)
    constexpr int64_t kLargeOffset = (int64_t(1) << 32) + 4096;
    Check(mojorecomp::host::SeekFile64(file, kLargeOffset, SEEK_SET) == 0 &&
              mojorecomp::host::TellFile64(file) == kLargeOffset,
          "SeekFile64/TellFile64 handle offsets past 4 GiB");
    Check(mojorecomp::host::ResizeFile(file, 8192), "ResizeFile grows");
    Check(mojorecomp::host::SeekFile64(file, 0, SEEK_END) == 0 &&
              mojorecomp::host::TellFile64(file) == 8192,
          "TellFile64 reports the grown size");
    Check(mojorecomp::host::ResizeFile(file, 16), "ResizeFile shrinks");
    Check(mojorecomp::host::SeekFile64(file, 0, SEEK_END) == 0 &&
              mojorecomp::host::TellFile64(file) == 16,
          "TellFile64 reports the shrunk size");
    Check(std::fflush(file) == 0 && mojorecomp::host::SyncFileToStorage(file),
          "SyncFileToStorage succeeds on a flushed stream");
    std::fclose(file);

    std::error_code error;
    std::filesystem::remove(path, error);
}

void TestDynamicLibrary()
{
    Check(mojorecomp::host::SharedLibraryFileName("dxcompiler") == kExpectedDxcName,
          "SharedLibraryFileName follows the host convention");

    mojorecomp::host::DynamicLibrary missing;
    Check(!missing.Open("mojorecomp-library-that-does-not-exist") && !missing.IsOpen(),
          "DynamicLibrary reports a missing library");
    Check(missing.Symbol(kSystemSymbol) == nullptr, "empty DynamicLibrary has no symbols");

    mojorecomp::host::DynamicLibrary system;
    Check(system.Open(kSystemLibrary), "DynamicLibrary opens a system library");
    Check(system.Symbol(kSystemSymbol) != nullptr, "DynamicLibrary resolves a symbol");
    Check(system.Symbol("mojorecomp_symbol_that_does_not_exist") == nullptr,
          "DynamicLibrary reports a missing symbol");

    mojorecomp::host::DynamicLibrary moved(std::move(system));
    Check(moved.IsOpen() && !system.IsOpen(), "DynamicLibrary moves ownership");
    moved.Close();
    Check(!moved.IsOpen(), "DynamicLibrary closes");
}

} // namespace

int main()
{
    TestExecutablePath();
    TestThreads();
    TestFiles();
    TestDynamicLibrary();
    return g_failures == 0 ? 0 : 1;
}
