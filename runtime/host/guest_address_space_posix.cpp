#include "guest_address_space.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace mojorecomp::host {
namespace {

std::string Describe(const char* step)
{
    return std::string(step) + " (" + std::strerror(errno) + ")";
}

} // namespace

uint8_t* ReserveGuestAddressSpace(size_t totalBytes, size_t physicalBytes,
                                  std::span<const size_t> physicalViews,
                                  std::string& error)
{
    if (physicalViews.empty() ||
        physicalViews.back() + physicalBytes != totalBytes)
    {
        error = "physical views must end at the top of the guest address space";
        return nullptr;
    }
    for (size_t i = 1; i < physicalViews.size(); ++i)
    {
        if (physicalViews[i] != physicalViews[i - 1] + physicalBytes)
        {
            error = "physical views must be contiguous";
            return nullptr;
        }
    }

    void* mapping = mmap(nullptr, totalBytes, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mapping == MAP_FAILED)
    {
        error = Describe("guest address space mapping failed");
        return nullptr;
    }
    auto* base = static_cast<uint8_t*>(mapping);

    const int fd = memfd_create("mojorecomp_physical", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, static_cast<off_t>(physicalBytes)) != 0)
    {
        error = Describe("physical backing failed");
        if (fd >= 0)
            close(fd);
        munmap(base, totalBytes);
        return nullptr;
    }
    for (size_t view : physicalViews)
    {
        if (mmap(base + view, physicalBytes, PROT_READ | PROT_WRITE,
                 MAP_SHARED | MAP_FIXED | MAP_NORESERVE, fd, 0) == MAP_FAILED)
        {
            error = Describe("physical alias map failed");
            close(fd);
            munmap(base, totalBytes);
            return nullptr;
        }
    }
    close(fd);
    return base;
}

} // namespace mojorecomp::host
