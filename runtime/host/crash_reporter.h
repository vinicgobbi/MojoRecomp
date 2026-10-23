#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace mojorecomp::host {

struct CrashReporterConfig
{
    std::filesystem::path outputDirectory;
    std::string runtimeVersion;
    std::string buildId;
    std::string runtimeSha256;
};

// Writes crash-<time>-pid<pid>.log and .json (plus .dmp on Windows) into the
// output directory when the process dies from an unhandled fault. Calling it
// again updates the configuration. On POSIX hosts the reporter must be
// installed before InstallWriteFaultHandler, so that write faults are claimed
// first and only unclaimed faults are reported; it refuses to replace a
// SIGSEGV handler installed by someone else.
bool InitializeCrashReporter(const CrashReporterConfig& config);
void ShutdownCrashReporter();

// Lets crashes caused by exhausting the calling thread's own stack still be
// reported. The thread that calls InitializeCrashReporter is covered already;
// other threads that run guest code call this when they start. No-op on hosts
// that do not need it.
void PrepareCurrentThreadForCrashReports();

std::filesystem::path CrashReportDirectoryForConfig(
    const std::filesystem::path& configPath);

void RecordVulkanFailure(const char* call, int32_t result,
                         const char* resultName, uint64_t frame,
                         uint64_t sequence, const char* stage) noexcept;

} // namespace mojorecomp::host
