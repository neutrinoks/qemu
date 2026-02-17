/*
 * Rockchip RK3399 General Register Files (GRF / PMUGRF / PMUSGRF).
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/arm/rk3399_grf.h"
#include "hw/core/qdev.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"

#define REG_INDEX(offset)   ((offset) / sizeof(uint32_t))

static uint32_t rk3399_grf_write_masked(uint32_t old, uint64_t val)
{
    uint32_t mask = (val >> 16) & 0xFFFF;
    uint32_t data = val & 0xFFFF;

    return (old & ~mask) | (data & mask);
}

static uint64_t rk3399_grf_read(void *opaque, hwaddr offset, unsigned size)
{
    Rk3399GrfState *s = RK3399_GRF(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= s->num_regs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds read at 0x%04x\n",
                      __func__, (uint32_t)offset);
        return 0;
    }

    return s->regs[idx];
}

static void rk3399_grf_write(void *opaque, hwaddr offset,
                              uint64_t val, unsigned size)
{
    Rk3399GrfState *s = RK3399_GRF(opaque);
    uint32_t idx = REG_INDEX(offset);

    if (idx >= s->num_regs) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: out-of-bounds write at 0x%04x\n",
                      __func__, (uint32_t)offset);
        return;
    }

    s->regs[idx] = rk3399_grf_write_masked(s->regs[idx], val);
}

static const MemoryRegionOps rk3399_grf_ops = {
    .read = rk3399_grf_read,
    .write = rk3399_grf_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl.min_access_size = 4,
};

static void rk3399_grf_realize(DeviceState *dev, Error **errp)
{
    Rk3399GrfState *s = RK3399_GRF(dev);

    if (s->num_regs == 0 || s->num_regs > RK3399_GRF_REGS_MAX) {
        error_setg(errp, "rk3399-grf: invalid num-regs %u", s->num_regs);
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &rk3399_grf_ops, s,
                          "rk3399.grf", s->num_regs * sizeof(uint32_t));
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void rk3399_grf_reset(DeviceState *dev)
{
    Rk3399GrfState *s = RK3399_GRF(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static const Property rk3399_grf_properties[] = {
    DEFINE_PROP_UINT32("num-regs", Rk3399GrfState, num_regs, 0),
};

static const VMStateDescription rk3399_grf_vmstate = {
    .name = "rk3399-grf",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(num_regs, Rk3399GrfState),
        VMSTATE_UINT32_ARRAY(regs, Rk3399GrfState, RK3399_GRF_REGS_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void rk3399_grf_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rk3399_grf_realize;
    device_class_set_legacy_reset(dc, rk3399_grf_reset);
    dc->vmsd = &rk3399_grf_vmstate;
    device_class_set_props(dc, rk3399_grf_properties);
}

static const TypeInfo rk3399_grf_types_info[] = {
    {
        .name = TYPE_RK3399_GRF,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Rk3399GrfState),
        .class_init = rk3399_grf_class_init,
    }
};

DEFINE_TYPES(rk3399_grf_types_info);
