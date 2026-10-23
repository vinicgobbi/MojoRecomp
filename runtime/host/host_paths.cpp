#include "host_paths.h"

#include "platform.h"

namespace HostPaths {

std::filesystem::path ExeDir()
{
    const auto executable = mojorecomp::host::ExecutablePath();
    if (!executable.empty())
        return executable.parent_path();
    return std::filesystem::current_path();
}

} // namespace HostPaths

