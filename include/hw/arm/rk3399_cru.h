/*
 * Rockchip RK3399 Clock & Reset Unit (CRU / PMUCRU).
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#ifndef HW_ARM_RK3399_CRU_H
#define HW_ARM_RK3399_CRU_H

#include "qom/object.h"
#include "hw/core/sysbus.h"

/*
 * CRU / PMUCRU memory map
 *
 * From DT bindings (rockchip,rk3399-cru.txt) and TRM Chapter 2:
 *
 *   CRU:    0xFF76_0000  (4 KiB) — main clock & reset unit
 *           7 PLLs: LPLL, BPLL, DPLL, CPLL, GPLL, NPLL, VPLL
 *           Clock selectors, gates, and soft-reset registers.
 *
 *   PMUCRU: 0xFF75_0000  (4 KiB) — PMU clock & reset unit
 *           1 PLL: PPLL
 *           Clocks for the always-on (PMU) power domain.
 *
 * This model provides a minimal register-level emulation:
 * read/write backing store with Rockchip write-mask semantics
 * and automatic PLL lock bit assertion.
 *
 * It does NOT model the actual clock tree or frequencies.
 */

#define RK3399_CRU_BASE             0xFF760000
#define RK3399_CRU_SIZE             0x1000

#define RK3399_PMUCRU_BASE          0xFF750000
#define RK3399_PMUCRU_SIZE          0x1000

#define RK3399_CRU_REGS_NUM        (RK3399_CRU_SIZE / sizeof(uint32_t))
#define RK3399_PMUCRU_REGS_NUM     (RK3399_PMUCRU_SIZE / sizeof(uint32_t))

/*
 * PLL register layout (TRM Chapter 2, U-Boot cru_rk3399.h)
 *
 * Each PLL occupies 0x20 bytes (8 x 32-bit registers, CON0..CON5 used,
 * CON6/CON7 reserved).
 *
 * CRU PLL base offsets:
 *   LPLL  0x000   Cortex-A53 little cluster
 *   BPLL  0x020   Cortex-A72 big cluster
 *   DPLL  0x040   DDR
 *   CPLL  0x060   Codec
 *   GPLL  0x080   General
 *   NPLL  0x0A0   New
 *   VPLL  0x0C0   Video
 *
 * PMUCRU PLL base offset:
 *   PPLL  0x000   PMU
 */
#define RK3399_CRU_NUM_PLLS        7
#define RK3399_PLL_STRIDE           0x20

#define RK3399_CRU_LPLL_OFF        0x000
#define RK3399_CRU_BPLL_OFF        0x020
#define RK3399_CRU_DPLL_OFF        0x040
#define RK3399_CRU_CPLL_OFF        0x060
#define RK3399_CRU_GPLL_OFF        0x080
#define RK3399_CRU_NPLL_OFF        0x0A0
#define RK3399_CRU_VPLL_OFF        0x0C0

#define RK3399_PMUCRU_PPLL_OFF     0x000

/* PLL CON2[31]: lock status (read-only, asserted when PLL is locked) */
#define RK3399_PLL_CON2_OFF        (2 * 4)
#define RK3399_PLL_LOCK_BIT        (1u << 31)

/* PLL CON3[9:8]: mode  (0 = slow / 24 MHz,  1 = normal,  2 = deep slow) */
#define RK3399_PLL_CON3_OFF        (3 * 4)
#define RK3399_PLL_MODE_MASK       (3u << 8)
#define RK3399_PLL_MODE_NORMAL     (1u << 8)

/* ---- QOM ---- */

#define TYPE_RK3399_CRU     "rk3399-cru"
OBJECT_DECLARE_SIMPLE_TYPE(Rk3399CruState, RK3399_CRU)

#define TYPE_RK3399_PMUCRU  "rk3399-pmucru"
OBJECT_DECLARE_SIMPLE_TYPE(Rk3399PmuCruState, RK3399_PMUCRU)

struct Rk3399CruState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[RK3399_CRU_REGS_NUM];
};

struct Rk3399PmuCruState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t regs[RK3399_PMUCRU_REGS_NUM];
};

#endif
