#include "guest_address_space.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace mojorecomp::host {
namespace {

std::string Describe(const char* step)
{
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%s (%lu)", step, GetLastError());
    return buffer;
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

    HANDLE process = GetCurrentProcess();
    auto* base = static_cast<uint8_t*>(VirtualAlloc2(
        process, nullptr, totalBytes,
        MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));
    if (!base)
    {
        error = Describe("guest placeholder reservation failed");
        return nullptr;
    }

    // Split the low range from the placeholder and replace it with normal
    // committed virtual memory. Pages consume physical backing only when touched.
    const size_t lowBytes = physicalViews.front();
    if (!VirtualFree(base, lowBytes, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER) ||
        !VirtualAlloc2(process, base, lowBytes,
                       MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER,
                       PAGE_READWRITE, nullptr, 0))
    {
        error = Describe("low guest mapping failed");
        return nullptr;
    }

    HANDLE physical = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE | SEC_COMMIT,
                                         DWORD(uint64_t(physicalBytes) >> 32),
                                         DWORD(physicalBytes), nullptr);
    if (!physical)
    {
        error = Describe("physical backing section failed");
        return nullptr;
    }

    for (size_t i = 0; i < physicalViews.size(); ++i)
    {
        // The last view is already the exact remaining placeholder.
        const bool last = i + 1 == physicalViews.size();
        if (!last &&
            !VirtualFree(base + physicalViews[i], physicalBytes,
                         MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
        {
            error = Describe("physical placeholder split failed");
            CloseHandle(physical);
            return nullptr;
        }
        if (!MapViewOfFile3(physical, process, base + physicalViews[i], 0,
                            physicalBytes, MEM_REPLACE_PLACEHOLDER,
                            PAGE_READWRITE, nullptr, 0))
        {
            error = Describe("physical alias map failed");
            CloseHandle(physical);
            return nullptr;
        }
    }
    CloseHandle(physical);
    return base;
}

} // namespace mojorecomp::host
