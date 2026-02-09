/*
 * Rockchip RK3399 SoC.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 *
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "hw/arm/rk3399.h"
#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/core/boards.h"
#include "system/address-spaces.h"

/* ---- SoC ---- */

static void rk3399_init(Object *obj)
{
    Rk3399State *s = RK3399(obj);

    object_initialize_child(obj, "cpu", &s->cpu,
                            ARM_CPU_TYPE_NAME("cortex-a53"));
}

static void rk3399_realize(DeviceState *dev, Error **errp)
{
    Rk3399State *s = RK3399(dev);

    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }
}

static void rk3399_class_init(ObjectClass *klass, const void *class_data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = rk3399_realize;
}

static const TypeInfo rk3399_types_info[] = {
    {
        .name = TYPE_RK3399,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Rk3399State),
        .instance_init = rk3399_init,
        .class_init = rk3399_class_init,
    }
};

DEFINE_TYPES(rk3399_types_info);

/* ---- Machine ---- */

static void rk3399_machine_init(MachineState *machine)
{
    static struct arm_boot_info boot_info;
    Rk3399State *s;

    boot_info = (struct arm_boot_info) {
        .loader_start = RK3399_RAM_START,
        .board_id = -1,
        .ram_size = machine->ram_size,
        .psci_conduit = QEMU_PSCI_CONDUIT_SMC,
    };

    s = RK3399(object_new(TYPE_RK3399));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(s));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(s), &error_fatal);

    memory_region_add_subregion(get_system_memory(), RK3399_RAM_START,
                                machine->ram);

    arm_load_kernel(&s->cpu, machine, &boot_info);
}

static void rk3399_machine_class_init(MachineClass *mc)
{
    mc->desc = "Rockchip RK3399 (Cortex-A53)";
    mc->init = rk3399_machine_init;
    mc->default_cpus = RK3399_NUM_CPUS;
    mc->max_cpus = RK3399_NUM_CPUS;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a53");
    mc->default_ram_size = 1 * GiB;
    mc->default_ram_id = "rk3399.ram";
}

DEFINE_MACHINE_AARCH64("rk3399", rk3399_machine_class_init)
