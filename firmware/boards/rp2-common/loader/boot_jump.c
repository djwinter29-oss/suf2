#include "boot_jump.h"

#include "hardware/platform_defs.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/systick.h"
#include "hardware/sync.h"

/* NVIC clear-enable and clear-pending registers (Armv6-M and Armv8-M). */
#define NVIC_ICER ((volatile uint32_t *)(PPB_BASE + 0xE180u))
#define NVIC_ICPR ((volatile uint32_t *)(PPB_BASE + 0xE280u))

bool boot_vector_table_ok(uint32_t vtor, uint32_t slot_start, uint32_t slot_end)
{
    const uint32_t *vt = (const uint32_t *)vtor;
    uint32_t sp = vt[0], pc = vt[1];
    bool sp_ok = sp > SRAM_BASE && sp <= SRAM_END && (sp & 3u) == 0;
    bool pc_ok = (pc & 1u) && (pc & ~1u) >= slot_start && (pc & ~1u) < slot_end;
    return sp_ok && pc_ok;
}

void boot_jump(uint32_t vtor)
{
    const uint32_t *vt = (const uint32_t *)vtor;
    uint32_t sp = vt[0], pc = vt[1];

    (void)save_and_disable_interrupts();
    systick_hw->csr = 0;
    for (unsigned i = 0; i < (NUM_IRQS + 31u) / 32u; i++) {
        NVIC_ICER[i] = 0xffffffffu;
        NVIC_ICPR[i] = 0xffffffffu;
    }
    scb_hw->vtor = vtor;
    __asm volatile("dsb\n isb" ::: "memory");
#if defined(__ARM_ARCH_8M_MAIN__)
    /* The loader's stack limit would fault the application's first push. */
    __asm volatile("msr msplim, %0" ::"r"(0u));
#endif
    __asm volatile(
        "msr msp, %0\n"
        "cpsie i\n"
        "bx %1\n" ::"r"(sp), "r"(pc));
    __builtin_unreachable();
}
