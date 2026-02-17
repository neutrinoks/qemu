/*
 * Rockchip RK3399 CRU / PMUCRU (Clock & Reset Unit) emulation.
 *
 * Minimal model: register read/write with Rockchip write-mask semantics
 * and PLL lock-bit side effects.
 *
 * Rockchip CRU registers use a write-mask scheme: when writing, bits
 * [31:16] are a mask — only bits where the corresponding mask bit is 1
 * are updated in the lower half.
 *
 * When software switches a PLL to normal mode (CON3[9:8] = 01), we
 * instantly assert the lock bit in CON2[31].  On real hardware this
 * takes microseconds; for emulation the instantaneous response is
 * sufficient.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/rk3399_cru.h"
#include "hw/core/qdev.h"
#include "migration/vmstate.h"

#define REG_INDEX(offset)   ((offset) / sizeof(uint32_t))

/*
 * PLL base offsets within the CRU address space.
 */
static const uint16_t rk3399_cru_pll_offsets[RK3399_CRU_NUM_PLLS] = {
    RK3399_CRU_LPLL_OFF,
    RK3399_CRU_BPLL_OFF,
    RK3399_CRU_DPLL_OFF,
    RK3399_CRU_CPLL_OFF,
    RK3399_CRU_GPLL_OFF,
    RK3399_CRU_NPLL_OFF,
    RK3399_CRU_VPLL_OFF,
};

/*
 * Apply Rockchip write-mask: val[31:16] = mask, val[15:0] = data.
 */
static uint32_t rk3399_write_masked(uint32_t old, uint64_t val)
{
    uint32_t mask = (val >> 16) & 0xFFFF;
    uint32_t data = val & 0xFFFF;

    return (old & ~mask) | (data & mask);
}

/*
 * Update PLL lock bit based on mode selection.
 * CON3[9:8] == 01 (normal) → set CON2[31] (locked).
 * Any other mode            → clear CON2[31].
 */
static void rk3399_cru_update_pll_lock(uint32_t *regs, uint16_t pll_base)
{
    uint32_t con3 = regs[REG_INDEX(pll_base + RK3399_PLL_CON3_OFF)];
    uint32_t con2_idx = REG_INDEX(pll_base + RK3399_PLL_CON2_OFF);

    if ((con3 & RK3399_PLL_MODE_MASK) == RK3399_PLL_MODE_NORMAL) {
        regs[con2_idx] |= RK3399_PLL_LOCK_BIT;
    } else {
        regs[con2_idx] &= ~RK3399_PLL_LOCK_BIT;
    }
}

/* ---- CRU (main) ---- */

static uint64_t rk3399_cru_read(void *opaque, hwaddr offset, unsigned size)
{
    Rk3399CruState *s = RK3399_CRU(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_CRU_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds read at 0x%03x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void rk3399_cru_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    Rk3399CruState *s = RK3399_CRU(opaque);
    uint32_t idx = REG_INDEX(offset);
    int i;

    if (idx >= RK3399_CRU_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds write at 0x%03x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    s->regs[idx] = rk3399_write_masked(s->regs[idx], val);

    /* Check if this write targets any PLL's CON3 (mode register) */
    for (i = 0; i < RK3399_CRU_NUM_PLLS; i++) {
        if (offset == rk3399_cru_pll_offsets[i] + RK3399_PLL_CON3_OFF) {
            rk3399_cru_update_pll_lock(s->regs,
                                        rk3399_cru_pll_offsets[i]);
            break;
        }
    }
}

static const MemoryRegionOps rk3399_cru_ops = {
    .read = rk3399_cru_read,
    .write = rk3399_cru_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void rk3399_cru_reset(DeviceState *dev)
{
    Rk3399CruState *s = RK3399_CRU(dev);

    /* All PLLs start in slow mode (24 MHz).  Zero is correct. */
    memset(s->regs, 0, sizeof(s->regs));
}

static void rk3399_cru_init(Object *obj)
{
    Rk3399CruState *s = RK3399_CRU(obj);

    memory_region_init_io(&s->iomem, obj, &rk3399_cru_ops, s,
                          "rk3399.cru", RK3399_CRU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rk3399_cru_vmstate = {
    .name = "rk3399-cru",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Rk3399CruState, RK3399_CRU_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void rk3399_cru_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rk3399_cru_reset);
    dc->vmsd = &rk3399_cru_vmstate;
}

/* ---- PMUCRU ---- */

static uint64_t rk3399_pmucru_read(void *opaque, hwaddr offset, unsigned size)
{
    Rk3399PmuCruState *s = RK3399_PMUCRU(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_PMUCRU_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds read at 0x%03x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void rk3399_pmucru_write(void *opaque, hwaddr offset,
                                 uint64_t val, unsigned size)
{
    Rk3399PmuCruState *s = RK3399_PMUCRU(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= RK3399_PMUCRU_REGS_NUM) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds write at 0x%03x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    s->regs[idx] = rk3399_write_masked(s->regs[idx], val);

    /* PPLL lock emulation */
    if (offset == RK3399_PMUCRU_PPLL_OFF + RK3399_PLL_CON3_OFF) {
        rk3399_cru_update_pll_lock(s->regs, RK3399_PMUCRU_PPLL_OFF);
    }
}

static const MemoryRegionOps rk3399_pmucru_ops = {
    .read = rk3399_pmucru_read,
    .write = rk3399_pmucru_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void rk3399_pmucru_reset(DeviceState *dev)
{
    Rk3399PmuCruState *s = RK3399_PMUCRU(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void rk3399_pmucru_init(Object *obj)
{
    Rk3399PmuCruState *s = RK3399_PMUCRU(obj);

    memory_region_init_io(&s->iomem, obj, &rk3399_pmucru_ops, s,
                          "rk3399.pmucru", RK3399_PMUCRU_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
}

static const VMStateDescription rk3399_pmucru_vmstate = {
    .name = "rk3399-pmucru",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, Rk3399PmuCruState,
                             RK3399_PMUCRU_REGS_NUM),
        VMSTATE_END_OF_LIST()
    }
};

static void rk3399_pmucru_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, rk3399_pmucru_reset);
    dc->vmsd = &rk3399_pmucru_vmstate;
}

/* ---- Registration ---- */

static const TypeInfo rk3399_cru_types_info[] = {
    {
        .name = TYPE_RK3399_CRU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Rk3399CruState),
        .instance_init = rk3399_cru_init,
        .class_init = rk3399_cru_class_init,
    },
    {
        .name = TYPE_RK3399_PMUCRU,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Rk3399PmuCruState),
        .instance_init = rk3399_pmucru_init,
        .class_init = rk3399_pmucru_class_init,
    }
};

DEFINE_TYPES(rk3399_cru_types_info);
