#include "memory.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "../host/guest_address_space.h"
#include "../runtime_state.h"

GuestMemory g_guestMemory;

namespace {

constexpr std::size_t kPhysicalSize = 0x20000000ull;
constexpr std::size_t kPhysicalViews[] = {
    0xA0000000ull,
    0xC0000000ull,
    0xE0000000ull,
};

static_assert(kPhysicalViews[2] + kPhysicalSize == PPC_MEMORY_SIZE);

bool CheckPhysicalViews(uint8_t* base) {
    constexpr std::size_t probe = 0x4000;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t value = 0x4D4F4A00u | i;
        *reinterpret_cast<volatile uint32_t*>(base + kPhysicalViews[i] + probe) = value;
        for (std::size_t view : kPhysicalViews) {
            if (*reinterpret_cast<volatile uint32_t*>(base + view + probe) != value)
                return false;
        }
    }
    for (std::size_t view : kPhysicalViews)
        *reinterpret_cast<volatile uint32_t*>(base + view + probe) = 0;
    return true;
}

} // namespace

bool GuestMemory::InsertFunction(uint32_t address, PPCFunc* host) noexcept {
    if (!base || address < PPC_CODE_BASE || address >= PPC_CODE_BASE + PPC_CODE_SIZE)
        return false;
    PPC_LOOKUP_FUNC(base, address) = host;
    return true;
}

void GuestMemory::Init() {
    if (base)
        return;

    std::string error;
    base = mojorecomp::host::ReserveGuestAddressSpace(
        PPC_MEMORY_SIZE, kPhysicalSize, kPhysicalViews, error);
    if (!base) {
        std::fprintf(stderr, "memory: %s\n", error.c_str());
        std::abort();
    }

    g_mojoGuestBase = base;
    if (!CheckPhysicalViews(base)) {
        std::fputs("memory: physical alias self-check failed\n", stderr);
        std::abort();
    }

    std::size_t installed = 0;
    std::size_t refused = 0;
    for (const PPCFuncMapping* m = PPCFuncMappings; m->host; ++m) {
        if (InsertFunction(static_cast<uint32_t>(m->guest), m->host))
            ++installed;
        else
            ++refused;
    }

    std::fprintf(stderr,
                 "memory: guest base=%p, physical aliases OK, functions=%zu, refused=%zu\n",
                 static_cast<void*>(base), installed, refused);
}

extern "C" void* MmGetHostAddress(uint32_t address) {
    return g_guestMemory.Translate(address);
}
