/*
 * Rockchip RK3399 General Register Files (GRF / PMUGRF / PMUSGRF).
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#ifndef HW_ARM_RK3399_GRF_H
#define HW_ARM_RK3399_GRF_H

#include "qom/object.h"
#include "hw/core/sysbus.h"

/*
 * General Register Files memory map
 *
 * The RK3399 has several "GRF" register blocks that provide miscellaneous
 * SoC configuration (IO voltage selection, pin muxing, DDR IO config,
 * system status storage, etc.).
 *
 * Firmware (TPL, SPL, TF-A) uses these extensively during DDR init:
 *   - GRF:     DDR IO configuration registers
 *   - PMUGRF:  os_reg2 / os_reg3 — stores detected DRAM parameters
 *              for later boot stages to read
 *   - PMUSGRF: soc_con4 — DDR address stride configuration
 *
 * From DTS (rk3399.dtsi) and TRM:
 *   GRF:     0xFF77_0000  (64 KiB)
 *   PMUGRF:  0xFF32_0000  (4 KiB)
 *   PMUSGRF: 0xFF33_0000  (4 KiB)
 *
 * All three use Rockchip write-mask semantics on most registers.
 * This model is a simple read/write store — no side effects.
 */

#define RK3399_GRF_BASE             0xFF770000
#define RK3399_GRF_SIZE             0x10000

#define RK3399_PMUGRF_BASE          0xFF320000
#define RK3399_PMUGRF_SIZE          0x1000

#define RK3399_PMUSGRF_BASE         0xFF330000
#define RK3399_PMUSGRF_SIZE         0x1000

/*
 * Generic Rockchip GRF device.
 *
 * All three GRF blocks share the same behaviour (write-masked register
 * store), so we use a single QOM type parameterised by "size".
 */
#define RK3399_GRF_REGS_MAX        (RK3399_GRF_SIZE / sizeof(uint32_t))

#define TYPE_RK3399_GRF     "rk3399-grf"
OBJECT_DECLARE_SIMPLE_TYPE(Rk3399GrfState, RK3399_GRF)

struct Rk3399GrfState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;

    uint32_t num_regs;
    uint32_t regs[RK3399_GRF_REGS_MAX];
};

#endif
