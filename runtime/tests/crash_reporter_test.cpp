// POSIX crash reporter: each scenario crashes a forked child and checks that
// the child still dies from the original signal and leaves a crash log and
// JSON sidecar. Also covers stack overflow on another thread, chaining behind
// the write fault handler, and refusing to replace a foreign SIGSEGV handler.
// Uses no guest code or game data.
#include "../host/crash_report_guest.h"
#include "../host/crash_reporter.h"
#include "../host/platform.h"

#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace mojorecomp::host::crash_report {

uint64_t GuestFrameCount() noexcept
{
    return 77;
}

bool ExtendedInfoEnabled() noexcept
{
    return false;
}

void WriteGuestState(bool) noexcept
{
    uint32_t value = 0;
    const bool unmappedRead = SafeRead(&value, reinterpret_cast<const void*>(uintptr_t{0x10}),
                                       sizeof(value));
    Write("[guest-crash] test-guest-state unmapped_read=%s\n",
          unmappedRead ? "succeeded" : "refused");
}

} // namespace mojorecomp::host::crash_report

namespace {

namespace fs = std::filesystem;
using namespace mojorecomp::host;

int g_failures = 0;

void Check(bool condition, const std::string& description)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description.c_str());
    if (!condition)
        ++g_failures;
}

std::string ReadFile(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool Contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

bool InitializeInto(const fs::path& directory)
{
    CrashReporterConfig config;
    config.outputDirectory = directory;
    config.runtimeVersion = "0.0.0-test";
    config.buildId = "test-build";
    config.runtimeSha256 = "abc\"def";
    return InitializeCrashReporter(config);
}

// Runs a scenario in a child process and returns its wait status.
template <typename Scenario>
int RunChild(Scenario scenario)
{
    std::fflush(stdout);
    const pid_t child = fork();
    if (child == 0)
    {
        // Keep intentional crashes out of the system's core dump handler.
        prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);
        const int stderrCopy = dup(STDERR_FILENO);
        std::freopen("/dev/null", "w", stderr);
        scenario();
        dup2(stderrCopy, STDERR_FILENO);
        std::_Exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return status;
}

struct Report
{
    std::string log;
    std::string json;
    size_t logCount = 0;
};

Report CollectReport(const fs::path& directory)
{
    Report report;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error))
    {
        const auto name = entry.path().filename().string();
        if (name.rfind("crash-", 0) != 0 || entry.path().extension() != ".log")
            continue;
        ++report.logCount;
        report.log = ReadFile(entry.path());
        auto json = entry.path();
        json.replace_extension(".json");
        report.json = ReadFile(json);
    }
    return report;
}

fs::path FreshDirectory(const fs::path& root, const char* name)
{
    const auto directory = root / name;
    fs::remove_all(directory);
    fs::create_directories(directory);
    return directory;
}

bool DiedFrom(int status, int signal)
{
    return WIFSIGNALED(status) && WTERMSIG(status) == signal;
}

void CheckNullRead(const fs::path& root)
{
    const auto directory = FreshDirectory(root, "null-read");
    const int status = RunChild([&] {
        if (!InitializeInto(directory))
            std::_Exit(3);
        RecordVulkanFailure("vkQueuePresentKHR", -4, "VK_ERROR_DEVICE_LOST", 12, 34, "present");
        volatile int* invalid = reinterpret_cast<volatile int*>(uintptr_t{0x10});
        std::_Exit(*invalid);
    });
    Check(DiedFrom(status, SIGSEGV), "null read still terminates the process with SIGSEGV");
    const Report report = CollectReport(directory);
    Check(report.logCount == 1, "null read leaves exactly one crash log");
    Check(Contains(report.log, "MojoRecomp Crash Report"), "log has the report header");
    Check(Contains(report.log, "signal=SIGSEGV(11) code=SEGV_MAPERR"), "log names the signal");
    Check(Contains(report.log, "access=read target=10"), "log has the access kind and address");
    Check(Contains(report.log, "frame=77"), "log has the guest frame");
    Check(Contains(report.log, "[last-vulkan-failure] call=vkQueuePresentKHR"),
          "log has the last Vulkan failure");
    Check(Contains(report.log, "test-guest-state unmapped_read=refused"),
          "guest state is written and SafeRead refuses unmapped memory");
    Check(Contains(report.log, "faulting_module=MojoRecompCrashReporterTest"),
          "log names the faulting module");
    Check(Contains(report.log, "[host-backtrace] frames="), "log has a backtrace");
    Check(Contains(report.json, "\"exception_code\": \"SIGSEGV\""), "JSON has the signal");
    Check(Contains(report.json, "\"access\": \"read\""), "JSON has the access kind");
    Check(Contains(report.json, "\"invalid_address\": \"0x10\""), "JSON has the address");
    Check(Contains(report.json, "\"runtime_sha256\": \"abc\\\"def\""),
          "JSON escapes string values");
    Check(Contains(report.json, "\"last_vulkan_call\": \"vkQueuePresentKHR\""),
          "JSON has the last Vulkan call");
    Check(Contains(report.json, "\"dump_created\": false"), "JSON reports no minidump");
    Check(Contains(report.json, "\"executable\": \"cot-runtime\""), "JSON names the executable");
}

void CheckAbort(const fs::path& root)
{
    const auto directory = FreshDirectory(root, "abort");
    const int status = RunChild([&] {
        if (!InitializeInto(directory))
            std::_Exit(3);
        std::abort();
    });
    Check(DiedFrom(status, SIGABRT), "abort still terminates the process with SIGABRT");
    const Report report = CollectReport(directory);
    Check(report.logCount == 1 && Contains(report.log, "signal=SIGABRT(6)"),
          "abort leaves a crash log");
    Check(Contains(report.json, "\"invalid_address\": null"),
          "abort JSON has no invalid address");
}

[[gnu::noinline]] int Recurse(int depth)
{
    volatile char buffer[4096];
    buffer[0] = static_cast<char>(depth);
    if (depth < 0)
        return 0;
    return Recurse(depth + 1) + buffer[0];
}

void CheckStackOverflowOnThread(const fs::path& root)
{
    const auto directory = FreshDirectory(root, "stack-overflow");
    const int status = RunChild([&] {
        if (!InitializeInto(directory))
            std::_Exit(3);
        std::thread worker([] {
            PrepareCurrentThreadForCrashReports();
            Recurse(0);
        });
        worker.join();
    });
    Check(DiedFrom(status, SIGSEGV), "stack overflow terminates the process with SIGSEGV");
    const Report report = CollectReport(directory);
    Check(report.logCount == 1 && Contains(report.log, "signal=SIGSEGV(11)"),
          "stack overflow on a prepared thread leaves a crash log");
}

void* g_watchedPage = nullptr;

bool ClaimWatchedPage(uintptr_t address) noexcept
{
    const auto page = reinterpret_cast<uintptr_t>(g_watchedPage);
    if (address < page || address >= page + 4096)
        return false;
    return SetPageAccess(g_watchedPage, 4096, PageAccess::ReadWrite);
}

void CheckWriteFaultChaining(const fs::path& root)
{
    const auto directory = FreshDirectory(root, "write-fault");
    const int status = RunChild([&] {
        if (!InitializeInto(directory))
            std::_Exit(3);
        g_watchedPage = mmap(nullptr, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (g_watchedPage == MAP_FAILED || !InstallWriteFaultHandler(ClaimWatchedPage))
            std::_Exit(4);
        *static_cast<volatile int*>(g_watchedPage) = 1;
        // Shutting down must keep the write fault handler working.
        ShutdownCrashReporter();
        SetPageAccess(g_watchedPage, 4096, PageAccess::ReadOnly);
        *static_cast<volatile int*>(g_watchedPage) = 2;
        // Initializing again reactivates the reporter behind that handler.
        if (!InitializeInto(directory))
            std::_Exit(5);
        if (!CollectReport(directory).log.empty())
            std::_Exit(6);
        volatile int* invalid = reinterpret_cast<volatile int*>(uintptr_t{0x20});
        *invalid = 3;
        std::_Exit(7);
    });
    Check(DiedFrom(status, SIGSEGV),
          "claimed write faults continue and an unclaimed write fault still terminates");
    const Report report = CollectReport(directory);
    Check(report.logCount == 1 && Contains(report.log, "access=write target=20"),
          "an unclaimed fault behind the write fault handler is reported once");
}

void CheckRefusesForeignHandler(const fs::path& root)
{
    const auto directory = FreshDirectory(root, "foreign-handler");
    const int status = RunChild([&] {
        struct sigaction action{};
        action.sa_handler = [](int) {};
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, nullptr);
        std::_Exit(InitializeInto(directory) ? 1 : 0);
    });
    Check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "initialization refuses to replace an existing SIGSEGV handler");
}

} // namespace

int main()
{
    const auto root = fs::temp_directory_path() /
                      ("mojorecomp-crash-reporter-" + std::to_string(getpid()));
    CheckNullRead(root);
    CheckAbort(root);
    CheckStackOverflowOnThread(root);
    CheckWriteFaultChaining(root);
    CheckRefusesForeignHandler(root);
    std::error_code error;
    fs::remove_all(root, error);
    if (g_failures)
    {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("All crash reporter checks passed");
    return 0;
}
