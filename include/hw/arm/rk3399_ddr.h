/*
 * Rockchip RK3399 DDR Memory Subsystem stub.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#ifndef HW_ARM_RK3399_DDR_H
#define HW_ARM_RK3399_DDR_H

#include "qom/object.h"
#include "hw/core/sysbus.h"

/*
 * RK3399 DDR Memory Subsystem
 *
 * The RK3399 has a dual-channel DDR memory interface.  Each channel
 * contains three Cadence Denali IP blocks (PCTL, PI, PHY) plus a
 * memory scheduler (MSCH).  A shared Channel Interconnect Controller
 * (CIC) synchronises frequency changes across channels.
 *
 * From U-Boot DT bindings and sdram_rk3399.c:
 *
 *   Channel 0:
 *     PCTL  0xFFA8_0000  (0x0800)   Denali DDR Protocol Controller
 *     PI    0xFFA8_0800  (0x1800)   Denali PHY Independent module
 *     PHY   0xFFA8_2000  (0x2000)   Denali DDR PHY controller
 *     MSCH  0xFFA8_4000  (0x1000)   Memory Scheduler (NoC QoS)
 *
 *   Channel 1:
 *     PCTL  0xFFA8_8000  (0x0800)
 *     PI    0xFFA8_8800  (0x1800)
 *     PHY   0xFFA8_A000  (0x2000)
 *     MSCH  0xFFA8_C000  (0x1000)
 *
 *   CIC    0xFF62_0000  (0x0100)    Channel Interconnect Controller
 *
 * This is a minimal stub that provides read/write register storage
 * with the specific side effects needed to prevent firmware from
 * spinning on status/completion polling loops:
 *
 *   1. PCTL denali_ctl[68] start  → sets denali_ctl[203] init-complete
 *   2. PCTL denali_ctl[118] MR read → sets ctl[203] complete, ctl[119] data
 *   3. PI   denali_pi[60] training → sets pi[174] + ctl[203] complete
 *   4. CIC  cic_ctrl0 write        → sets cic_status0 freq-switch-done
 */

#define RK3399_DDR_NUM_CHANNELS     2

/* Per-channel register block base addresses */
#define RK3399_DDR_PCTL_BASE(ch)    (0xFFA80000 + (ch) * 0x8000)
#define RK3399_DDR_PI_BASE(ch)      (0xFFA80800 + (ch) * 0x8000)
#define RK3399_DDR_PHY_BASE(ch)     (0xFFA82000 + (ch) * 0x8000)
#define RK3399_DDR_MSCH_BASE(ch)    (0xFFA84000 + (ch) * 0x8000)

#define RK3399_DDR_PCTL_SIZE        0x0800
#define RK3399_DDR_PI_SIZE          0x1800
#define RK3399_DDR_PHY_SIZE         0x2000
#define RK3399_DDR_MSCH_SIZE        0x1000

/* CIC (Channel Interconnect Controller) */
#define RK3399_DDR_CIC_BASE         0xFF620000
#define RK3399_DDR_CIC_SIZE         0x0100

/* Register counts (32-bit registers) */
#define RK3399_DDR_PCTL_REGS        (RK3399_DDR_PCTL_SIZE / 4)
#define RK3399_DDR_PI_REGS          (RK3399_DDR_PI_SIZE / 4)
#define RK3399_DDR_PHY_REGS         (RK3399_DDR_PHY_SIZE / 4)
#define RK3399_DDR_MSCH_REGS        (RK3399_DDR_MSCH_SIZE / 4)
#define RK3399_DDR_CIC_REGS         (RK3399_DDR_CIC_SIZE / 4)

/*
 * Key register indices in the Denali controller arrays
 * (from U-Boot drivers/ram/rockchip/sdram_rk3399.c)
 */

/* PCTL: denali_ctl[68] bit 0 — start controller */
#define PCTL_START_IDX              68
#define PCTL_START_BIT              (1u << 0)

/* PCTL: denali_ctl[118] — mode register read command */
#define PCTL_MR_CMD_IDX             118

/* PCTL: denali_ctl[119] — mode register read data */
#define PCTL_MR_DATA_IDX            119

/* PCTL: denali_ctl[203] — interrupt / status register */
#define PCTL_INT_STATUS_IDX         203
#define PCTL_INIT_COMPLETE_BIT      (1u << 21)
#define PCTL_MR_READ_ERROR_BIT      (1u << 12)
#define PCTL_TRAINING_COMPLETE_BIT  (1u << 13)

/* PI: denali_pi[60] — training trigger */
#define PI_TRAINING_TRIGGER_IDX     60

/* PI: denali_pi[174] — training status / completion */
#define PI_TRAINING_STATUS_IDX      174

/* CIC registers (offsets in words) */
#define CIC_CTRL0_IDX               0
#define CIC_STATUS0_IDX             4
#define CIC_STATUS0_FREQ_DONE_BIT   (1u << 2)

/* ---- QOM ---- */

#define TYPE_RK3399_DDR     "rk3399-ddr"
OBJECT_DECLARE_SIMPLE_TYPE(Rk3399DdrState, RK3399_DDR)

struct Rk3399DdrChannel {
    uint32_t pctl[RK3399_DDR_PCTL_REGS];
    uint32_t pi[RK3399_DDR_PI_REGS];
    uint32_t phy[RK3399_DDR_PHY_REGS];
    uint32_t msch[RK3399_DDR_MSCH_REGS];
};

struct Rk3399DdrState {
    SysBusDevice parent_obj;

    struct Rk3399DdrChannel chan[RK3399_DDR_NUM_CHANNELS];
    uint32_t cic[RK3399_DDR_CIC_REGS];

    /* One MemoryRegion per block, mapped to system memory in realize */
    MemoryRegion pctl_iomem[RK3399_DDR_NUM_CHANNELS];
    MemoryRegion pi_iomem[RK3399_DDR_NUM_CHANNELS];
    MemoryRegion phy_iomem[RK3399_DDR_NUM_CHANNELS];
    MemoryRegion msch_iomem[RK3399_DDR_NUM_CHANNELS];
    MemoryRegion cic_iomem;
};

#endif
