/*
 * Rockchip RK3399 DDR Memory Subsystem stub.
 *
 * Minimal emulation of the dual-channel Cadence Denali DDR controller
 * complex.  Provides register read/write storage with side effects at
 * the specific polling points that U-Boot's sdram_rk3399.c checks:
 *
 *   1. PCTL start (ctl[68])       → init-complete in ctl[203]
 *   2. Mode register read (ctl[118]) → data in ctl[119], done in ctl[203]
 *   3. PI training trigger (pi[60])  → completion in pi[174] & ctl[203]
 *   4. CIC frequency switch         → done in cic_status0
 *
 * Everything else is a plain register store — writes are accepted and
 * reads return the last written value.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/rk3399_ddr.h"
#include "hw/core/qdev.h"
#include "migration/vmstate.h"

#define REG_INDEX(offset)   ((offset) / sizeof(uint32_t))

/*
 * Fake mode register values for DRAM geometry detection.
 *
 * The firmware reads MR5 (manufacturer) and MR8 (type/density/IO width)
 * to detect what DRAM is attached.  We return values that describe a
 * plausible LPDDR3 configuration:
 *
 *   MR5 = 0x01  (Samsung)
 *   MR8 = 0x18  (S16, LPDDR3, x32 IO width — encodes density/type)
 *
 * These values cause U-Boot to detect a working DRAM channel.
 * The actual memory behind address 0x0000_0000 is QEMU's ram backing,
 * so address-pattern geometry probes work against real memory.
 */
static uint32_t rk3399_ddr_fake_mr(uint32_t mr_num)
{
    switch (mr_num) {
    case 5:     /* MR5: manufacturer ID */
        return 0x01;    /* Samsung */
    case 8:     /* MR8: type, density, IO width */
        return 0x18;
    default:
        return 0;
    }
}

/* ---- PCTL (Protocol Controller) ---- */

static uint64_t rk3399_ddr_pctl_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_PCTL_REGS) {
        return 0;
    }

    return ch->pctl[idx];
}

static void rk3399_ddr_pctl_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_PCTL_REGS) {
        return;
    }

    ch->pctl[idx] = (uint32_t)val;

    /*
     * Side effect 1: PCTL start.
     * When denali_ctl[68] bit 0 is set, the controller begins DRAM
     * initialisation.  Firmware then polls ctl[203] bit 21.
     */
    if (idx == PCTL_START_IDX && (val & PCTL_START_BIT)) {
        ch->pctl[PCTL_INT_STATUS_IDX] |= PCTL_INIT_COMPLETE_BIT;
    }

    /*
     * Side effect 2: Mode register read.
     * Firmware writes ctl[118] to issue an MR read, then polls ctl[203]
     * bit 21 for completion.  Result appears in ctl[119].
     */
    if (idx == PCTL_MR_CMD_IDX) {
        uint32_t mr_num = (val >> 8) & 0xFF;

        ch->pctl[PCTL_MR_DATA_IDX] = rk3399_ddr_fake_mr(mr_num);
        ch->pctl[PCTL_INT_STATUS_IDX] |= PCTL_INIT_COMPLETE_BIT;
        ch->pctl[PCTL_INT_STATUS_IDX] &= ~PCTL_MR_READ_ERROR_BIT;
    }
}

static const MemoryRegionOps rk3399_ddr_pctl_ops = {
    .read = rk3399_ddr_pctl_read,
    .write = rk3399_ddr_pctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

/* ---- PI (PHY Independent) ---- */

static uint64_t rk3399_ddr_pi_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_PI_REGS) {
        return 0;
    }

    return ch->pi[idx];
}

static void rk3399_ddr_pi_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_PI_REGS) {
        return;
    }

    ch->pi[idx] = (uint32_t)val;

    /*
     * Side effect 3: Training trigger.
     * Firmware writes training-enable bits into pi[60], then polls
     * pi[174] for the corresponding completion flags.
     *
     * Rather than tracking individual training types, we set all
     * known completion bits in pi[174] whenever pi[60] is written
     * with any non-zero value.  This satisfies all training phases
     * (CA, write leveling, read gate, read/write DQ).
     *
     * We also set the training-complete bit in ctl[203] since
     * some code paths check that as well.
     */
    if (idx == PI_TRAINING_TRIGGER_IDX && val != 0) {
        ch->pi[PI_TRAINING_STATUS_IDX] = 0xFFFFFFFF;
        ch->pctl[PCTL_INT_STATUS_IDX] |= PCTL_TRAINING_COMPLETE_BIT;
    }
}

static const MemoryRegionOps rk3399_ddr_pi_ops = {
    .read = rk3399_ddr_pi_read,
    .write = rk3399_ddr_pi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

/* ---- PHY (plain register store, no side effects) ---- */

static uint64_t rk3399_ddr_phy_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_PHY_REGS) {
        return 0;
    }

    return ch->phy[idx];
}

static void rk3399_ddr_phy_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_PHY_REGS) {
        return;
    }

    ch->phy[idx] = (uint32_t)val;
}

static const MemoryRegionOps rk3399_ddr_phy_ops = {
    .read = rk3399_ddr_phy_read,
    .write = rk3399_ddr_phy_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

/* ---- MSCH (Memory Scheduler, plain register store) ---- */

static uint64_t rk3399_ddr_msch_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_MSCH_REGS) {
        return 0;
    }

    return ch->msch[idx];
}

static void rk3399_ddr_msch_write(void *opaque, hwaddr offset,
                                   uint64_t val, unsigned size)
{
    struct Rk3399DdrChannel *ch = opaque;
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_MSCH_REGS) {
        return;
    }

    ch->msch[idx] = (uint32_t)val;
}

static const MemoryRegionOps rk3399_ddr_msch_ops = {
    .read = rk3399_ddr_msch_read,
    .write = rk3399_ddr_msch_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

/* ---- CIC (Channel Interconnect Controller) ---- */

static uint64_t rk3399_ddr_cic_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    Rk3399DdrState *s = RK3399_DDR(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_CIC_REGS) {
        return 0;
    }

    return s->cic[idx];
}

static void rk3399_ddr_cic_write(void *opaque, hwaddr offset,
                                  uint64_t val, unsigned size)
{
    Rk3399DdrState *s = RK3399_DDR(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_DDR_CIC_REGS) {
        return;
    }

    s->cic[idx] = (uint32_t)val;

    /*
     * Side effect 4: Frequency switch.
     * Firmware writes cic_ctrl0 to initiate a DDR frequency change,
     * then polls cic_status0 bit 2 for completion.
     */
    if (idx == CIC_CTRL0_IDX) {
        s->cic[CIC_STATUS0_IDX] |= CIC_STATUS0_FREQ_DONE_BIT;
    }
}

static const MemoryRegionOps rk3399_ddr_cic_ops = {
    .read = rk3399_ddr_cic_read,
    .write = rk3399_ddr_cic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

/* ---- Device lifecycle ---- */

static void rk3399_ddr_init(Object *obj)
{
    Rk3399DdrState *s = RK3399_DDR(obj);
    int ch;
    char name[32];

    for (ch = 0; ch < RK3399_DDR_NUM_CHANNELS; ch++) {
        snprintf(name, sizeof(name), "rk3399.ddr-pctl%d", ch);
        memory_region_init_io(&s->pctl_iomem[ch], obj,
                              &rk3399_ddr_pctl_ops, &s->chan[ch],
                              name, RK3399_DDR_PCTL_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->pctl_iomem[ch]);

        snprintf(name, sizeof(name), "rk3399.ddr-pi%d", ch);
        memory_region_init_io(&s->pi_iomem[ch], obj,
                              &rk3399_ddr_pi_ops, &s->chan[ch],
                              name, RK3399_DDR_PI_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->pi_iomem[ch]);

        snprintf(name, sizeof(name), "rk3399.ddr-phy%d", ch);
        memory_region_init_io(&s->phy_iomem[ch], obj,
                              &rk3399_ddr_phy_ops, &s->chan[ch],
                              name, RK3399_DDR_PHY_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->phy_iomem[ch]);

        snprintf(name, sizeof(name), "rk3399.ddr-msch%d", ch);
        memory_region_init_io(&s->msch_iomem[ch], obj,
                              &rk3399_ddr_msch_ops, &s->chan[ch],
                              name, RK3399_DDR_MSCH_SIZE);
        sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->msch_iomem[ch]);
    }

    memory_region_init_io(&s->cic_iomem, obj,
                          &rk3399_ddr_cic_ops, s,
                          "rk3399.ddr-cic", RK3399_DDR_CIC_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->cic_iomem);
}

static void rk3399_ddr_reset(DeviceState *dev)
{
    Rk3399DdrState *s = RK3399_DDR(dev);

    memset(s->chan, 0, sizeof(s->chan));
    memset(s->cic, 0, sizeof(s->cic));
}

static void rk3399_ddr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rk3399_ddr_reset);
}

static const TypeInfo rk3399_ddr_types_info[] = {
    {
        .name = TYPE_RK3399_DDR,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Rk3399DdrState),
        .instance_init = rk3399_ddr_init,
        .class_init = rk3399_ddr_class_init,
    }
};

DEFINE_TYPES(rk3399_ddr_types_info);
