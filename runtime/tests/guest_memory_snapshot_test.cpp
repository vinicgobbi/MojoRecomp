#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "../gpu/guest_memory_snapshot.h"
#include "../host/guest_address_space.h"

namespace {

int Fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

} // namespace

int main()
{
    using namespace mojorecomp::gpu;

    std::vector<uint8_t> guest(0x4000, 0);
    constexpr uint32_t address = 0x1000;
    for (uint32_t i = 0; i < 32; ++i)
        guest[address + i] = static_cast<uint8_t>(0x40u + i);

    GuestMemorySnapshot snapshot;
    if (!snapshot.Capture(guest.data(), address, 32))
        return Fail("capture failed");
    if (snapshot.ByteSize() != 32)
        return Fail("captured byte count is wrong");
    if (!snapshot.Capture(guest.data(), address + 4, 8) || snapshot.ByteSize() != 32)
        return Fail("contained capture was not deduplicated");

    GuestMemoryIdentity fullIdentity{};
    GuestMemoryIdentity subIdentity{};
    if (!snapshot.ResolveIdentity(address, 32, fullIdentity) || !fullIdentity)
        return Fail("captured range has no immutable identity");
    if (!snapshot.ResolveIdentity(address + 4, 8, subIdentity) ||
        subIdentity.token != fullIdentity.token || subIdentity.offset != 4)
        return Fail("contained range did not preserve identity and offset");

    guest[address + 7] = 0xEE;
    {
        ScopedGuestMemorySnapshot active(&snapshot);
        const uint8_t* frozen = GuestReadPtr(guest.data(), address, 32);
        if (!frozen || frozen[7] != 0x47)
            return Fail("active snapshot did not preserve submission-time bytes");
        GuestMemoryIdentity activeIdentity{};
        if (!GuestReadIdentity(address + 4, 8, activeIdentity) ||
            activeIdentity.token != fullIdentity.token || activeIdentity.offset != 4)
            return Fail("active snapshot identity did not match captured range");

        const uint8_t* missing = GuestReadPtr(guest.data(), address + 0x100, 4);
        if (!missing || missing[0] != 0 || !GuestSnapshotReadFailed())
            return Fail("missing active-snapshot read did not fail with immutable zero data");
    }

    GuestMemoryIdentity inactiveIdentity{};
    if (GuestReadIdentity(address, 32, inactiveIdentity))
        return Fail("inactive snapshot unexpectedly exposed an identity");

    const uint8_t* live = GuestReadPtr(guest.data(), address, 32);
    if (!live || live[7] != 0xEE)
        return Fail("live reads were not restored after snapshot scope");

    // Production guest layout: 4 GiB with three 512 MiB physical aliases.
    constexpr size_t kGuestBytes = size_t(1) << 32;
    constexpr size_t kPhysicalBytes = 0x20000000;
    constexpr size_t kPhysicalViews[] = {0xA0000000, 0xC0000000, 0xE0000000};
    std::string error;
    uint8_t* aliasBase = mojorecomp::host::ReserveGuestAddressSpace(
        kGuestBytes, kPhysicalBytes, kPhysicalViews, error);
    if (!aliasBase)
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return Fail("guest address space reservation failed");
    }

    aliasBase[0x82000000u] = 0x5A;
    if (aliasBase[0x82000000u] != 0x5A)
        return Fail("low guest memory is not writable");
    aliasBase[0xA0002000u] = 0x11;
    if (aliasBase[0xC0002000u] != 0x11 || aliasBase[0xE0002000u] != 0x11)
        return Fail("physical views do not alias the same backing");

    constexpr uint32_t trackedAddress = 0xA0001000u;
    aliasBase[trackedAddress] = 0x31;
    GuestMemorySnapshot beforeAliasWrite;
    if (!beforeAliasWrite.Capture(aliasBase, trackedAddress, 1))
        return Fail("tracked alias snapshot capture failed");
    const uint64_t missesAfterFirstCapture = GuestSnapshotCacheMissCount();

    GuestMemorySnapshot unchanged;
    if (!unchanged.Capture(aliasBase, trackedAddress, 1) ||
        GuestSnapshotCacheHitCount() == 0 ||
        GuestSnapshotCacheMissCount() != missesAfterFirstCapture)
        return Fail("unchanged tracked range was not served from the cache");

    // The write faults on the read-only tracked page, the write-fault handler
    // makes it writable again, and the write is retried.
    aliasBase[0xC0001000u] = 0x72;
    GuestMemorySnapshot afterAliasWrite;
    if (!afterAliasWrite.Capture(aliasBase, trackedAddress, 1))
        return Fail("tracked alias recapture failed");
    const uint8_t* refreshed = afterAliasWrite.Resolve(trackedAddress, 1);
    if (!refreshed || refreshed[0] != 0x72)
        return Fail("write through a physical alias reused stale snapshot bytes");
    const uint8_t* original = beforeAliasWrite.Resolve(trackedAddress, 1);
    if (!original || original[0] != 0x31)
        return Fail("earlier snapshot changed after a guest write");

    // Writes from other threads must be tracked the same way.
    constexpr uint32_t threadedAddress = 0xA0010000u;
    GuestMemorySnapshot beforeThreadWrites;
    if (!beforeThreadWrites.Capture(aliasBase, threadedAddress, 4 * 4096))
        return Fail("multi-page capture failed");
    std::vector<std::thread> writers;
    for (uint32_t i = 0; i < 4; ++i)
        writers.emplace_back([aliasBase, i] {
            aliasBase[0xE0010000u + i * 4096u + 8u] = static_cast<uint8_t>(0xA0u + i);
        });
    for (auto& writer : writers)
        writer.join();
    GuestMemorySnapshot afterThreadWrites;
    if (!afterThreadWrites.Capture(aliasBase, threadedAddress, 4 * 4096))
        return Fail("multi-page recapture failed");
    const uint8_t* pages = afterThreadWrites.Resolve(threadedAddress, 4 * 4096);
    for (uint32_t i = 0; pages && i < 4; ++i)
        if (pages[i * 4096u + 8u] != 0xA0u + i)
            return Fail("write from another thread reused stale snapshot bytes");
    GuestSnapshotCacheShutdown();

    // After shutdown the pages are writable again without any handler.
    aliasBase[trackedAddress] = 0x99;
    if (aliasBase[0xC0001000u] != 0x99)
        return Fail("tracked pages were not restored to read-write on shutdown");

    std::puts("PASS: guest memory snapshot freezes draw-visible bytes and restores live reads.");
    return 0;
}
