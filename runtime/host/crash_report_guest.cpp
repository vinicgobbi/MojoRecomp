// Guest details for the POSIX crash reporter (see crash_report_guest.h). Guest
// memory and the PPC context are read through SafeRead because the crash may
// have corrupted them.
#include "crash_report_guest.h"

#include "../debug_mode.h"
#include "../gpu/pm4.h"
#include "../kernel/guestcall.h"
#include "../kernel/memory.h"

extern "C" void MojoRecompGetLastIndirectCall(uint32_t* target, uint32_t* lr,
                                              uint32_t* object, uint64_t* sequence);

namespace mojorecomp::host::crash_report {
namespace {

const char* FunctionMapped(uint32_t address) noexcept
{
    uint8_t* base = g_guestMemory.base;
    if (!base)
        return "unknown";
    if (address < PPC_CODE_BASE || address >= PPC_CODE_BASE + PPC_CODE_SIZE)
        return "no";
    PPCFunc* function = nullptr;
    if (!SafeRead(&function, &PPC_LOOKUP_FUNC(base, address), sizeof(function)))
        return "unavailable";
    return function ? "yes" : "no";
}

} // namespace

uint64_t GuestFrameCount() noexcept
{
    return Pm4_FrameCount();
}

bool ExtendedInfoEnabled() noexcept
{
    return mojorecomp::debug::ExtendedCrashInfoEnabled();
}

void WriteGuestState(bool extended) noexcept
{
    uint32_t lastIndirectTarget = 0;
    uint32_t lastIndirectLr = 0;
    uint32_t lastIndirectObject = 0;
    uint64_t lastIndirectSequence = 0;
    MojoRecompGetLastIndirectCall(&lastIndirectTarget, &lastIndirectLr,
                                  &lastIndirectObject, &lastIndirectSequence);
    Write("[last-indirect] seq=%llu target=%08X lr=%08X object=%08X mapped=%s\n",
          static_cast<unsigned long long>(lastIndirectSequence),
          lastIndirectTarget, lastIndirectLr, lastIndirectObject,
          lastIndirectTarget ? FunctionMapped(lastIndirectTarget) : "no");

    const PPCContext* live = g_ppcContext;
    if (!live)
        return;
    PPCContext ppc;
    if (!SafeRead(&ppc, live, sizeof(ppc)))
    {
        Write("[guest-crash] context unavailable\n");
        return;
    }
    Write("[guest-crash] lr=%08X ctr=%08X ctr_mapped=%s r1=%08X r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X r11=%08X r12=%08X r13=%08X\n",
          static_cast<uint32_t>(ppc.lr), ppc.ctr.u32, FunctionMapped(ppc.ctr.u32),
          ppc.r1.u32, ppc.r3.u32, ppc.r4.u32, ppc.r5.u32, ppc.r6.u32, ppc.r7.u32,
          ppc.r8.u32, ppc.r9.u32, ppc.r10.u32, ppc.r11.u32, ppc.r12.u32, ppc.r13.u32);
    if (extended)
    {
        Write("[guest-debug] r14=%08X r15=%08X r16=%08X r17=%08X r18=%08X r19=%08X r20=%08X r21=%08X r22=%08X r23=%08X r24=%08X r25=%08X r26=%08X r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X\n",
              ppc.r14.u32, ppc.r15.u32, ppc.r16.u32, ppc.r17.u32, ppc.r18.u32,
              ppc.r19.u32, ppc.r20.u32, ppc.r21.u32, ppc.r22.u32, ppc.r23.u32,
              ppc.r24.u32, ppc.r25.u32, ppc.r26.u32, ppc.r27.u32, ppc.r28.u32,
              ppc.r29.u32, ppc.r30.u32, ppc.r31.u32);
    }
}

} // namespace mojorecomp::host::crash_report
