#include "crash_reporter.h"

#include "crash_report_guest.h"
#include "platform.h"
#include "../kernel/time_fields.h"

#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

// The report is written from the signal handler of the crashing thread. Files
// are written with open/write, guest and stack memory is read through
// SafeRead, and everything that may allocate or take locks (paths, the UTC
// offset, the executable range, the unwinder) is prepared during
// initialization. Formatting uses vsnprintf with integer and string
// conversions only, and backtrace_symbols_fd writes without allocating.

namespace mojorecomp::host {
namespace {

constexpr size_t kCrashPathCapacity = 1400;
constexpr size_t kTextFieldCapacity = 96;
constexpr size_t kModuleNameCapacity = 256;
// Room for the unwinder, a copy of the guest context, and formatting buffers.
constexpr size_t kAltStackBytes = 256 * 1024;
constexpr int kBacktraceDepth = 64;
constexpr unsigned kStackScanWords = 32;
// How long a thread that crashes while another thread writes the report waits
// for it before its own fault terminates the process.
constexpr unsigned kConcurrentCrashWaitMilliseconds = 5000;
// Seconds from 1601-01-01 (the kernel time epoch) to 1970-01-01.
constexpr int64_t kUnixEpochIn1601Seconds = 11'644'473'600;

constexpr int kCrashSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
constexpr size_t kCrashSignalCount = std::size(kCrashSignals);

struct VulkanFailureRecord
{
    std::array<char, kTextFieldCapacity> call{};
    std::array<char, kTextFieldCapacity> resultName{};
    std::array<char, kTextFieldCapacity> stage{};
    int32_t result = 0;
    uint64_t frame = 0;
    uint64_t sequence = 0;
    uint32_t threadId = 0;
};

struct ModuleInfo
{
    uintptr_t base = 0;
    std::array<char, kModuleNameCapacity> name{};
};

std::atomic<bool> g_active{false};
std::atomic<uint32_t> g_reportingThread{0};
std::atomic<bool> g_reportFinished{false};
std::array<bool, kCrashSignalCount> g_installed{};
std::array<struct sigaction, kCrashSignalCount> g_previousActions{};

std::array<char, kCrashPathCapacity> g_outputDirectory{};
std::array<char, kTextFieldCapacity> g_runtimeVersion{};
std::array<char, kTextFieldCapacity> g_buildId{};
std::array<char, kTextFieldCapacity> g_runtimeSha256{};
long g_utcOffsetSeconds = 0;
uintptr_t g_executableBias = 0;
uintptr_t g_executableTextStart = 0;
uintptr_t g_executableTextEnd = 0;

std::array<VulkanFailureRecord, 2> g_vulkanFailures{};
std::atomic<uint64_t> g_vulkanFailureGeneration{0};

int g_logFd = -1;

struct AltStack
{
    void* mapping = nullptr;
    size_t mappingBytes = 0;

    ~AltStack()
    {
        if (!mapping)
            return;
        stack_t disable{};
        disable.ss_flags = SS_DISABLE;
        sigaltstack(&disable, nullptr);
        munmap(mapping, mappingBytes);
    }
};

thread_local AltStack t_altStack;

template <size_t N>
void CopyText(std::array<char, N>& destination, const char* source) noexcept
{
    std::snprintf(destination.data(), destination.size(), "%s", source ? source : "");
}

void WriteAll(int fd, const char* data, size_t bytes) noexcept
{
    while (fd >= 0 && bytes > 0)
    {
        const ssize_t written = write(fd, data, bytes);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return;
        data += written;
        bytes -= static_cast<size_t>(written);
    }
}

__attribute__((format(printf, 2, 0)))
void FormatTo(int fd, const char* format, va_list args) noexcept
{
    char line[2048];
    const int length = std::vsnprintf(line, sizeof(line), format, args);
    if (length > 0)
        WriteAll(fd, line, std::min(static_cast<size_t>(length), sizeof(line) - 1));
}

__attribute__((format(printf, 2, 3)))
void FilePrintf(int fd, const char* format, ...) noexcept
{
    va_list args;
    va_start(args, format);
    FormatTo(fd, format, args);
    va_end(args);
}

// Writes "key": "value" with JSON escaping, or null for an empty value.
void JsonString(int fd, const char* key, const char* value) noexcept
{
    if (!value || !*value)
    {
        FilePrintf(fd, "  \"%s\": null,\n", key);
        return;
    }
    char escaped[kCrashPathCapacity];
    size_t length = 0;
    for (const char* c = value; *c && length + 7 < sizeof(escaped); ++c)
    {
        const auto byte = static_cast<unsigned char>(*c);
        if (byte == '"' || byte == '\\')
        {
            escaped[length++] = '\\';
            escaped[length++] = static_cast<char>(byte);
        }
        else if (byte < 0x20)
        {
            length += static_cast<size_t>(
                std::snprintf(escaped + length, sizeof(escaped) - length, "\\u%04x", byte));
        }
        else
        {
            escaped[length++] = static_cast<char>(byte);
        }
    }
    escaped[length] = '\0';
    FilePrintf(fd, "  \"%s\": \"%s\",\n", key, escaped);
}

const char* SignalName(int signal) noexcept
{
    switch (signal)
    {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    case SIGTRAP: return "SIGTRAP";
    default: return "unknown";
    }
}

const char* SignalCodeName(int signal, int code) noexcept
{
    switch (code)
    {
    case SI_USER: return "SI_USER";
    case SI_TKILL: return "SI_TKILL";
    case SI_QUEUE: return "SI_QUEUE";
    case SI_KERNEL: return "SI_KERNEL";
    default: break;
    }
    if (signal == SIGSEGV)
        return code == SEGV_MAPERR ? "SEGV_MAPERR" : code == SEGV_ACCERR ? "SEGV_ACCERR" : "unknown";
    if (signal == SIGBUS)
        return code == BUS_ADRALN ? "BUS_ADRALN" : code == BUS_ADRERR ? "BUS_ADRERR" :
               code == BUS_OBJERR ? "BUS_OBJERR" : "unknown";
    if (signal == SIGFPE)
        return code == FPE_INTDIV ? "FPE_INTDIV" : code == FPE_INTOVF ? "FPE_INTOVF" :
               code == FPE_FLTDIV ? "FPE_FLTDIV" : code == FPE_FLTINV ? "FPE_FLTINV" : "unknown";
    if (signal == SIGILL)
        return code == ILL_ILLOPC ? "ILL_ILLOPC" : code == ILL_ILLOPN ? "ILL_ILLOPN" :
               code == ILL_PRVOPC ? "ILL_PRVOPC" : "unknown";
    return "unknown";
}

// Read, write, or execute, from the x86 page-fault error code: bit 1 is set
// for writes and bit 4 for instruction fetches.
const char* AccessKind(const ucontext_t* context) noexcept
{
    const auto error = static_cast<uint64_t>(context->uc_mcontext.gregs[REG_ERR]);
    return (error & 0x10) ? "execute" : (error & 0x2) ? "write" : "read";
}

kernel::CalendarTime LocalCrashTime() noexcept
{
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    const int64_t localSeconds = int64_t(now.tv_sec) + g_utcOffsetSeconds;
    const auto calendar = kernel::TimeToCalendar(
        uint64_t(localSeconds + kUnixEpochIn1601Seconds) * 10'000'000ull);
    return calendar.value_or(kernel::CalendarTime{});
}

bool BuildCrashBasePath(char* path, size_t pathCapacity,
                        const kernel::CalendarTime& time) noexcept
{
    if (!g_outputDirectory[0])
        return false;
    mkdir(g_outputDirectory.data(), 0755);
    const int length = std::snprintf(
        path, pathCapacity, "%s/crash-%04u-%02u-%02u-%02u%02u%02u-pid%ld",
        g_outputDirectory.data(), unsigned(time.year), unsigned(time.month),
        unsigned(time.day), unsigned(time.hour), unsigned(time.minute),
        unsigned(time.second), static_cast<long>(getpid()));
    return length > 0 && static_cast<size_t>(length) < pathCapacity;
}

bool AppendExtension(const char* base, const char* extension,
                     char* destination, size_t destinationCapacity) noexcept
{
    const int length = std::snprintf(destination, destinationCapacity, "%s%s", base, extension);
    return length > 0 && static_cast<size_t>(length) < destinationCapacity;
}

uint64_t ParseHex(const char*& cursor) noexcept
{
    uint64_t value = 0;
    for (;; ++cursor)
    {
        const char c = *cursor;
        if (c >= '0' && c <= '9')
            value = value * 16 + uint64_t(c - '0');
        else if (c >= 'a' && c <= 'f')
            value = value * 16 + uint64_t(c - 'a' + 10);
        else
            return value;
    }
}

void SkipField(const char*& cursor) noexcept
{
    while (*cursor && *cursor != ' ')
        ++cursor;
    while (*cursor == ' ')
        ++cursor;
}

// Finds the mapped file that contains an address by reading /proc/self/maps,
// which also covers libraries loaded after initialization (Vulkan drivers,
// FFmpeg). The base is the start of the file's first mapping, so base-relative
// offsets match what addr2line expects for position-independent objects.
bool FindModule(uintptr_t address, ModuleInfo& module) noexcept
{
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;

    char chunk[4096];
    char line[1024];
    size_t lineLength = 0;
    char currentPath[kModuleNameCapacity * 2]{};
    uintptr_t currentBase = 0;
    bool found = false;

    auto parseLine = [&]() noexcept {
        line[lineLength] = '\0';
        const char* cursor = line;
        const uintptr_t start = ParseHex(cursor);
        if (*cursor == '-')
            ++cursor;
        const uintptr_t end = ParseHex(cursor);
        while (*cursor == ' ')
            ++cursor;
        SkipField(cursor); // permissions
        const uintptr_t offset = ParseHex(cursor);
        while (*cursor == ' ')
            ++cursor;
        SkipField(cursor); // device
        SkipField(cursor); // inode
        const char* path = cursor;

        if (std::strcmp(path, currentPath) != 0)
        {
            std::snprintf(currentPath, sizeof(currentPath), "%s", path);
            currentBase = start - offset;
        }
        if (address < start || address >= end)
            return;

        module.base = currentBase;
        const char* slash = std::strrchr(path, '/');
        const char* name = slash ? slash + 1 : path;
        std::snprintf(module.name.data(), module.name.size(), "%s", *name ? name : "[anonymous]");
        found = true;
    };

    for (;;)
    {
        const ssize_t bytes = read(fd, chunk, sizeof(chunk));
        if (bytes < 0 && errno == EINTR)
            continue;
        if (bytes <= 0)
            break;
        for (ssize_t i = 0; i < bytes && !found; ++i)
        {
            if (chunk[i] == '\n')
            {
                parseLine();
                lineLength = 0;
            }
            else if (lineLength + 1 < sizeof(line))
            {
                line[lineLength++] = chunk[i];
            }
        }
        if (found)
            break;
    }
    close(fd);
    return found;
}

VulkanFailureRecord LastVulkanFailure() noexcept
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const uint64_t generation = g_vulkanFailureGeneration.load(std::memory_order_acquire);
        if (!generation)
            return {};
        const VulkanFailureRecord record = g_vulkanFailures[generation & 1u];
        if (generation == g_vulkanFailureGeneration.load(std::memory_order_acquire))
            return record;
    }
    return {};
}

void WriteCrashSidecar(const char* jsonPath, const kernel::CalendarTime& crashTime,
                       int signal, const siginfo_t* info, const ucontext_t* context,
                       uint64_t frame, const VulkanFailureRecord& vulkanFailure,
                       const ModuleInfo* faultingModule) noexcept
{
    const int fd = open(jsonPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0)
        return;

    const bool memoryFault = signal == SIGSEGV || signal == SIGBUS;
    FilePrintf(fd, "{\n");
    FilePrintf(fd, "  \"schema_version\": 1,\n");
    FilePrintf(fd, "  \"timestamp_local\": \"%04u-%02u-%02uT%02u:%02u:%02u\",\n",
               unsigned(crashTime.year), unsigned(crashTime.month), unsigned(crashTime.day),
               unsigned(crashTime.hour), unsigned(crashTime.minute), unsigned(crashTime.second));
    JsonString(fd, "runtime_version", g_runtimeVersion.data());
    JsonString(fd, "build_id", g_buildId.data());
    JsonString(fd, "runtime_sha256", g_runtimeSha256.data());
    FilePrintf(fd, "  \"architecture\": \"x86_64\",\n");
    FilePrintf(fd, "  \"executable\": \"cot-runtime\",\n");
    FilePrintf(fd, "  \"exception_code\": \"%s\",\n", SignalName(signal));
    FilePrintf(fd, "  \"signal\": %d,\n", signal);
    FilePrintf(fd, "  \"signal_code\": %d,\n", info ? info->si_code : 0);
    if (memoryFault && info && info->si_code > 0)
    {
        if (signal == SIGSEGV)
            FilePrintf(fd, "  \"access\": \"%s\",\n", AccessKind(context));
        else
            FilePrintf(fd, "  \"access\": null,\n");
        FilePrintf(fd, "  \"invalid_address\": \"0x%llX\",\n",
                   static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(info->si_addr)));
    }
    else
    {
        FilePrintf(fd, "  \"access\": null,\n");
        FilePrintf(fd, "  \"invalid_address\": null,\n");
    }
    FilePrintf(fd, "  \"frame\": %llu,\n", static_cast<unsigned long long>(frame));
    FilePrintf(fd, "  \"thread_id\": %u,\n", CurrentThreadId());
    if (vulkanFailure.call[0])
    {
        JsonString(fd, "last_vulkan_call", vulkanFailure.call.data());
        FilePrintf(fd, "  \"vulkan_result\": %d,\n", vulkanFailure.result);
        JsonString(fd, "vulkan_result_name", vulkanFailure.resultName.data());
        JsonString(fd, "vulkan_stage", vulkanFailure.stage.data());
        FilePrintf(fd, "  \"gpu_sequence\": %llu,\n",
                   static_cast<unsigned long long>(vulkanFailure.sequence));
    }
    else
    {
        FilePrintf(fd, "  \"last_vulkan_call\": null,\n");
        FilePrintf(fd, "  \"vulkan_result\": null,\n");
        FilePrintf(fd, "  \"vulkan_result_name\": null,\n");
        FilePrintf(fd, "  \"vulkan_stage\": null,\n");
        FilePrintf(fd, "  \"gpu_sequence\": null,\n");
    }
    JsonString(fd, "faulting_module", faultingModule ? faultingModule->name.data() : nullptr);
    // Linux reports carry no minidump (decision D4).
    FilePrintf(fd, "  \"dump_created\": false,\n");
    FilePrintf(fd, "  \"dump_error\": \"unsupported\"\n");
    FilePrintf(fd, "}\n");
    close(fd);
}

void WriteCrashReport(int signal, const siginfo_t* info, const ucontext_t* context) noexcept
{
    const auto crashTime = LocalCrashTime();
    char basePath[kCrashPathCapacity]{};
    char logPath[kCrashPathCapacity]{};
    char jsonPath[kCrashPathCapacity]{};
    const bool havePaths = BuildCrashBasePath(basePath, sizeof(basePath), crashTime) &&
                           AppendExtension(basePath, ".log", logPath, sizeof(logPath)) &&
                           AppendExtension(basePath, ".json", jsonPath, sizeof(jsonPath));
    g_logFd = havePaths ? open(logPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644) : -1;

    const auto& registers = context->uc_mcontext.gregs;
    const auto rip = static_cast<uintptr_t>(registers[REG_RIP]);
    const auto rsp = static_cast<uintptr_t>(registers[REG_RSP]);
    const auto rbp = static_cast<uintptr_t>(registers[REG_RBP]);
    ModuleInfo faultingModule;
    const bool haveModule = FindModule(rip, faultingModule);
    const uint64_t frame = crash_report::GuestFrameCount();
    const bool extended = crash_report::ExtendedInfoEnabled();
    const VulkanFailureRecord vulkanFailure = LastVulkanFailure();
    char threadName[17]{};
    prctl(PR_GET_NAME, threadName, 0, 0, 0);
    const int code = info ? info->si_code : 0;

    crash_report::Write("MojoRecomp Crash Report\n");
    crash_report::Write("timestamp_local=%04u-%02u-%02u %02u:%02u:%02u\n",
                        unsigned(crashTime.year), unsigned(crashTime.month),
                        unsigned(crashTime.day), unsigned(crashTime.hour),
                        unsigned(crashTime.minute), unsigned(crashTime.second));
    crash_report::Write(
        "runtime_version=%s build_id=%s runtime_sha256=%s architecture=x86_64 executable=cot-runtime\n",
        g_runtimeVersion.data(), g_buildId.data(),
        g_runtimeSha256[0] ? g_runtimeSha256.data() : "unknown");
    crash_report::Write("build=%s %s\n", __DATE__, __TIME__);
    crash_report::Write("pid=%ld tid=%u thread=%s debug_mode=%u frame=%llu\n",
                        static_cast<long>(getpid()), CurrentThreadId(), threadName,
                        extended ? 1u : 0u, static_cast<unsigned long long>(frame));
    crash_report::Write(
        "[host-crash] signal=%s(%d) code=%s(%d) address=%llX module=%llX rva=%llX rip=%llX rsp=%llX rbp=%llX faulting_module=%s\n",
        SignalName(signal), signal, SignalCodeName(signal, code), code,
        static_cast<unsigned long long>(rip),
        static_cast<unsigned long long>(haveModule ? faultingModule.base : 0),
        static_cast<unsigned long long>(haveModule && rip >= faultingModule.base
                                            ? rip - faultingModule.base : 0),
        static_cast<unsigned long long>(rip), static_cast<unsigned long long>(rsp),
        static_cast<unsigned long long>(rbp),
        haveModule ? faultingModule.name.data() : "unknown");
    if ((signal == SIGSEGV || signal == SIGBUS) && code > 0)
    {
        crash_report::Write("[host-crash] access=%s target=%llX\n",
                            signal == SIGSEGV ? AccessKind(context) : "unknown",
                            static_cast<unsigned long long>(
                                reinterpret_cast<uintptr_t>(info->si_addr)));
    }
    if (vulkanFailure.call[0])
    {
        crash_report::Write(
            "[last-vulkan-failure] call=%s result=%d name=%s frame=%llu sequence=%llu thread=%u stage=%s\n",
            vulkanFailure.call.data(), vulkanFailure.result, vulkanFailure.resultName.data(),
            static_cast<unsigned long long>(vulkanFailure.frame),
            static_cast<unsigned long long>(vulkanFailure.sequence),
            vulkanFailure.threadId, vulkanFailure.stage.data());
    }

    // The sidecar goes out before the guest state and stack walk, which read
    // memory that may be corrupt.
    if (havePaths)
        WriteCrashSidecar(jsonPath, crashTime, signal, info, context, frame, vulkanFailure,
                          haveModule ? &faultingModule : nullptr);

    crash_report::WriteGuestState(extended);

    bool stackReadable = true;
    for (unsigned i = 0; i < kStackScanWords; ++i)
    {
        uintptr_t value = 0;
        if (!crash_report::SafeRead(&value, reinterpret_cast<const uintptr_t*>(rsp) + i,
                                    sizeof(value)))
        {
            stackReadable = false;
            break;
        }
        if (value >= g_executableTextStart && value < g_executableTextEnd)
        {
            crash_report::Write("[host-crash] stack[%02u]=%llX rva=%llX\n", i,
                                static_cast<unsigned long long>(value),
                                static_cast<unsigned long long>(value - g_executableBias));
        }
    }
    if (!stackReadable)
        crash_report::Write("[host-crash] stack unreadable\n");

    void* frames[kBacktraceDepth];
    const int frameCount = backtrace(frames, kBacktraceDepth);
    crash_report::Write("[host-backtrace] frames=%d executable_base=%llX\n", frameCount,
                        static_cast<unsigned long long>(g_executableBias));
    backtrace_symbols_fd(frames, frameCount, STDERR_FILENO);
    if (g_logFd >= 0)
        backtrace_symbols_fd(frames, frameCount, g_logFd);
    crash_report::Write("[minidump] status=unsupported\n");

    if (g_logFd >= 0)
    {
        close(g_logFd);
        g_logFd = -1;
        FilePrintf(STDERR_FILENO, "[crash-log] report=%s\n", logPath);
    }
    else
    {
        FilePrintf(STDERR_FILENO, "[crash-log] failed to create report file\n");
    }
}

size_t SignalIndex(int signal) noexcept
{
    for (size_t i = 0; i < kCrashSignalCount; ++i)
    {
        if (kCrashSignals[i] == signal)
            return i;
    }
    return kCrashSignalCount;
}

// Passes the signal on as if the reporter were not installed. Faults raised
// by an instruction re-execute it on return and then take the default action;
// signals that were sent (abort, kill, int3) are raised again.
void ForwardSignal(int signal, siginfo_t* info, void* context) noexcept
{
    const size_t index = SignalIndex(signal);
    if (index < kCrashSignalCount)
    {
        const struct sigaction& previous = g_previousActions[index];
        if ((previous.sa_flags & SA_SIGINFO) && previous.sa_sigaction)
        {
            previous.sa_sigaction(signal, info, context);
            return;
        }
        if (!(previous.sa_flags & SA_SIGINFO) && previous.sa_handler != SIG_DFL &&
            previous.sa_handler != SIG_IGN)
        {
            previous.sa_handler(signal);
            return;
        }
    }
    struct sigaction defaultAction{};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    sigaction(signal, &defaultAction, nullptr);
    if (!info || info->si_code <= 0 || signal == SIGABRT || signal == SIGTRAP)
        raise(signal);
}

void CrashSignalHandler(int signal, siginfo_t* info, void* context)
{
    const int savedErrno = errno;
    if (g_active.load(std::memory_order_acquire) && context)
    {
        const uint32_t self = CurrentThreadId();
        uint32_t expected = 0;
        if (g_reportingThread.compare_exchange_strong(expected, self))
        {
            WriteCrashReport(signal, info, static_cast<const ucontext_t*>(context));
            g_reportFinished.store(true, std::memory_order_release);
        }
        else if (expected != self)
        {
            for (unsigned waited = 0; waited < kConcurrentCrashWaitMilliseconds &&
                                      !g_reportFinished.load(std::memory_order_acquire);
                 waited += 10)
            {
                const timespec delay{0, 10'000'000};
                nanosleep(&delay, nullptr);
            }
        }
    }
    ForwardSignal(signal, info, context);
    errno = savedErrno;
}

bool IsCrashHandler(const struct sigaction& action) noexcept
{
    return (action.sa_flags & SA_SIGINFO) && action.sa_sigaction == CrashSignalHandler;
}

bool HasCustomHandler(const struct sigaction& action) noexcept
{
    if (action.sa_flags & SA_SIGINFO)
        return action.sa_sigaction != nullptr;
    return action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN;
}

int RecordExecutableRange(dl_phdr_info* info, size_t, void*)
{
    // The first object reported is the executable itself.
    g_executableBias = static_cast<uintptr_t>(info->dlpi_addr);
    uintptr_t start = UINTPTR_MAX;
    uintptr_t end = 0;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i)
    {
        const auto& header = info->dlpi_phdr[i];
        if (header.p_type != PT_LOAD || !(header.p_flags & PF_X))
            continue;
        start = std::min<uintptr_t>(start, g_executableBias + header.p_vaddr);
        end = std::max<uintptr_t>(end, g_executableBias + header.p_vaddr + header.p_memsz);
    }
    if (start < end)
    {
        g_executableTextStart = start;
        g_executableTextEnd = end;
    }
    return 1;
}

bool InstallAltStack() noexcept
{
    if (t_altStack.mapping)
        return true;
    stack_t current{};
    if (sigaltstack(nullptr, &current) == 0 && !(current.ss_flags & SS_DISABLE))
        return true; // Another component already gave this thread one.

    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    const size_t bytes = kAltStackBytes + page;
    void* mapping = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mapping == MAP_FAILED)
        return false;
    // Guard page below the stack, so overflowing it faults instead of
    // corrupting adjacent memory.
    mprotect(mapping, page, PROT_NONE);

    stack_t stack{};
    stack.ss_sp = static_cast<char*>(mapping) + page;
    stack.ss_size = kAltStackBytes;
    if (sigaltstack(&stack, nullptr) != 0)
    {
        munmap(mapping, bytes);
        return false;
    }
    t_altStack.mapping = mapping;
    t_altStack.mappingBytes = bytes;
    return true;
}

} // namespace

namespace crash_report {

void Write(const char* format, ...) noexcept
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    FormatTo(STDERR_FILENO, format, args);
    if (g_logFd >= 0)
        FormatTo(g_logFd, format, copy);
    va_end(copy);
    va_end(args);
}

bool SafeRead(void* destination, const void* source, size_t bytes) noexcept
{
    // The kernel copies the memory and reports EFAULT for unmapped pages, so a
    // corrupt pointer cannot fault inside the handler.
    iovec local{destination, bytes};
    iovec remote{const_cast<void*>(source), bytes};
    return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == static_cast<ssize_t>(bytes);
}

} // namespace crash_report

std::filesystem::path CrashReportDirectoryForConfig(
    const std::filesystem::path& configPath)
{
    return configPath.parent_path() / "logs";
}

bool InitializeCrashReporter(const CrashReporterConfig& config)
{
    std::filesystem::path directory = config.outputDirectory;
    if (directory.empty())
    {
        const auto executable = ExecutablePath();
        if (executable.empty())
            return false;
        directory = executable.parent_path() / "crashlogs";
    }
    const std::string output = directory.string();
    if (output.size() >= g_outputDirectory.size())
        return false;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error)
        return false;

    CopyText(g_outputDirectory, output.c_str());
    CopyText(g_runtimeVersion, config.runtimeVersion.c_str());
    CopyText(g_buildId, config.buildId.c_str());
    CopyText(g_runtimeSha256, config.runtimeSha256.c_str());

    tm local{};
    const time_t now = time(nullptr);
    g_utcOffsetSeconds = localtime_r(&now, &local) ? local.tm_gmtoff : 0;
    dl_iterate_phdr(RecordExecutableRange, nullptr);
    // The first backtrace() call loads the unwinder, which allocates.
    void* warmUp[1];
    backtrace(warmUp, 1);

    if (!InstallAltStack())
        return false;

    struct sigaction action{};
    action.sa_sigaction = CrashSignalHandler;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int signal : kCrashSignals)
        sigaddset(&action.sa_mask, signal);

    for (size_t i = 0; i < kCrashSignalCount; ++i)
    {
        if (g_installed[i])
            continue;
        struct sigaction current{};
        if (sigaction(kCrashSignals[i], nullptr, &current) != 0)
            return false;
        if (kCrashSignals[i] == SIGSEGV && HasCustomHandler(current))
        {
            // Probably the write fault handler: replacing it would turn every
            // tracked guest write into a crash report.
            std::fprintf(stderr,
                         "[crash-log] not installed: a SIGSEGV handler is already present\n");
            return false;
        }
        if (sigaction(kCrashSignals[i], &action, &g_previousActions[i]) != 0)
            return false;
        g_installed[i] = true;
    }
    g_active.store(true, std::memory_order_release);
    return true;
}

void ShutdownCrashReporter()
{
    g_active.store(false, std::memory_order_release);
    for (size_t i = 0; i < kCrashSignalCount; ++i)
    {
        if (!g_installed[i])
            continue;
        // A handler installed later (the write fault handler) chains to this
        // one; leave it in place, inactive, rather than unhooking that handler.
        struct sigaction current{};
        if (sigaction(kCrashSignals[i], nullptr, &current) == 0 && IsCrashHandler(current))
        {
            sigaction(kCrashSignals[i], &g_previousActions[i], nullptr);
            g_installed[i] = false;
        }
    }
}

void PrepareCurrentThreadForCrashReports()
{
    InstallAltStack();
}

void RecordVulkanFailure(const char* call, int32_t result,
                         const char* resultName, uint64_t frame,
                         uint64_t sequence, const char* stage) noexcept
{
    const uint64_t next = g_vulkanFailureGeneration.load(std::memory_order_relaxed) + 1;
    VulkanFailureRecord& record = g_vulkanFailures[next & 1u];
    CopyText(record.call, call);
    CopyText(record.resultName, resultName);
    CopyText(record.stage, stage);
    record.result = result;
    record.frame = frame;
    record.sequence = sequence;
    record.threadId = CurrentThreadId();
    g_vulkanFailureGeneration.store(next, std::memory_order_release);
}

} // namespace mojorecomp::host
