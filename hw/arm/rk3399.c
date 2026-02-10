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
#include "hw/char/serial-mm.h"
#include "hw/core/boards.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/arm_gicv3.h"
#include "hw/intc/arm_gicv3_common.h"
#include "qobject/qlist.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "target/arm/gtimer.h"

/*
 * UART descriptor table
 *
 * All five RK3399 UARTs are Synopsys DesignWare APB UARTs, 16550-compatible.
 * They share the same register layout (reg-shift 2, 32-bit wide) and differ
 * only in base address, interrupt line, and which CharBackend they connect to.
 *
 * UART2 is the conventional debug console and is mapped to the first
 * QEMU serial backend (serial_hd(0)).  The remaining UARTs are mapped
 * to serial_hd(1)..serial_hd(4) in ascending UART order, skipping UART2.
 * UARTs without a corresponding -serial argument become no-ops (the
 * serial_mm_init call is skipped when serial_hd() returns NULL).
 */
static const struct {
    hwaddr      base;
    int         spi;        /* GIC SPI number (= QEMU GPIO input index) */
    int         serial_idx; /* index into serial_hd() */
} rk3399_uarts[RK3399_NUM_UARTS] = {
    { RK3399_UART0_BASE, RK3399_GIC_SPI_UART0, 1 },
    { RK3399_UART1_BASE, RK3399_GIC_SPI_UART1, 2 },
    { RK3399_UART2_BASE, RK3399_GIC_SPI_UART2, 0 },   /* debug console */
    { RK3399_UART3_BASE, RK3399_GIC_SPI_UART3, 3 },
    { RK3399_UART4_BASE, RK3399_GIC_SPI_UART4, 4 },
};

/* ---- SoC ---- */

static void rk3399_init(Object *obj)
{
    Rk3399State *s = RK3399(obj);

    object_initialize_child(obj, "cpu", &s->cpu,
                            ARM_CPU_TYPE_NAME("cortex-a53"));
    object_initialize_child(obj, "gic", &s->gic, gicv3_class_name());
}

static void rk3399_realize(DeviceState *dev, Error **errp)
{
    Rk3399State *s = RK3399(dev);
    DeviceState *cpudev = DEVICE(&s->cpu);
    DeviceState *gicdev = DEVICE(&s->gic);
    SysBusDevice *gicbusdev = SYS_BUS_DEVICE(&s->gic);
    QList *redist_region_count;
    int i;

    /*
     * Mapping from CPU generic timer output lines to GIC PPI inputs.
     * These are PPI INTID offsets (added to the per-CPU PPI base below).
     */
    const int timer_irq[] = {
        [GTIMER_PHYS] = RK3399_GIC_PPI_PHYSTIMER,
        [GTIMER_VIRT] = RK3399_GIC_PPI_VIRTTIMER,
        [GTIMER_HYP]  = RK3399_GIC_PPI_HYPTIMER,
        [GTIMER_SEC]  = RK3399_GIC_PPI_SECTIMER,
    };

    /* ---- CPU ---- */
    if (!qdev_realize(cpudev, NULL, errp)) {
        return;
    }

    /* ---- Embedded SRAM ---- */

    /*
     * INTMEM0: 192 KiB main SRAM at 0xFF8C_0000
     *
     * This is the primary on-chip SRAM used during early boot.
     * The BootROM loads the TPL/SPL into this region, and TF-A (BL31)
     * places its SRAM text/data sections here.  It is also used by
     * the Cortex-M0 coprocessor for DDR frequency scaling code.
     */
    memory_region_init_ram(&s->intmem0, OBJECT(dev), "rk3399.intmem0",
                           RK3399_INTMEM0_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(),
                                RK3399_INTMEM0_BASE, &s->intmem0);

    /*
     * INTMEM1: 64 KiB secondary SRAM at 0xFF3B_0000
     *
     * Secondary SRAM in the perilp power domain.  Available as
     * general-purpose SRAM.  TF-A may also use parts of this region.
     */
    memory_region_init_ram(&s->intmem1, OBJECT(dev), "rk3399.intmem1",
                           RK3399_INTMEM1_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(),
                                RK3399_INTMEM1_BASE, &s->intmem1);

    /* ---- GICv3 (GIC-500) ---- */

    qdev_prop_set_uint32(gicdev, "num-irq",
                         RK3399_GIC_NUM_SPI + GIC_INTERNAL);
    qdev_prop_set_uint32(gicdev, "revision", 3);
    qdev_prop_set_uint32(gicdev, "num-cpu", RK3399_NUM_CPUS);

    redist_region_count = qlist_new();
    qlist_append_int(redist_region_count, RK3399_NUM_CPUS);
    qdev_prop_set_array(gicdev, "redist-region-count", redist_region_count);

    if (!sysbus_realize(gicbusdev, errp)) {
        return;
    }

    sysbus_mmio_map(gicbusdev, 0, RK3399_GIC_DIST_BASE);
    sysbus_mmio_map(gicbusdev, 1, RK3399_GIC_REDIST_BASE);

    /* ---- Wire CPU timer outputs -> GIC PPI inputs ---- */
    {
        int cpu_idx = 0;
        int ppibase = RK3399_GIC_NUM_SPI
                    + cpu_idx * GIC_INTERNAL
                    + GIC_NR_SGIS;
        int irq;

        for (irq = 0; irq < ARRAY_SIZE(timer_irq); irq++) {
            qdev_connect_gpio_out(cpudev, irq,
                                  qdev_get_gpio_in(gicdev,
                                                   ppibase + timer_irq[irq]));
        }

        /* GICv3 maintenance interrupt via named CPU GPIO output */
        qdev_connect_gpio_out_named(cpudev,
                                    "gicv3-maintenance-interrupt", 0,
                                    qdev_get_gpio_in(gicdev,
                                                     ppibase + RK3399_GIC_PPI_MAINT));
    }

    /* ---- Wire GIC outputs -> CPU interrupt inputs ---- */
    sysbus_connect_irq(gicbusdev, 0,
                       qdev_get_gpio_in(cpudev, ARM_CPU_IRQ));
    sysbus_connect_irq(gicbusdev, RK3399_NUM_CPUS,
                       qdev_get_gpio_in(cpudev, ARM_CPU_FIQ));
    sysbus_connect_irq(gicbusdev, 2 * RK3399_NUM_CPUS,
                       qdev_get_gpio_in(cpudev, ARM_CPU_VIRQ));
    sysbus_connect_irq(gicbusdev, 3 * RK3399_NUM_CPUS,
                       qdev_get_gpio_in(cpudev, ARM_CPU_VFIQ));

    /*
     * ---- UARTs (Synopsys DesignWare 16550) ----
     *
     * All five RK3399 UARTs share the same hardware: snps,dw-apb-uart
     * with reg-shift=2 (32-bit registers at 4-byte aligned offsets).
     *
     * UART2 is the conventional debug console (serial_hd(0)).
     * UARTs without a backend (serial_hd() == NULL) are silently
     * skipped — the MMIO region simply won't exist, which is fine
     * because nothing will probe those addresses without a DT node.
     */
    for (i = 0; i < RK3399_NUM_UARTS; i++) {
        Chardev *chr = serial_hd(rk3399_uarts[i].serial_idx);
        if (!chr) {
            continue;
        }
        serial_mm_init(get_system_memory(),
                       rk3399_uarts[i].base,
                       RK3399_UART_REG_SHIFT,
                       qdev_get_gpio_in(gicdev, rk3399_uarts[i].spi),
                       115200, chr, DEVICE_LITTLE_ENDIAN);
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

    /*
     * Clamp RAM size to the maximum usable low-DDR region.
     *
     * On the real RK3399, physical addresses 0xF800_0000..0xFFFF_FFFF
     * are reserved for MMIO (peripherals, SRAM, BootROM, GIC).
     * DDR below this hole can be at most ~3968 MiB (0xF800_0000 bytes).
     *
     * If the user requests more via -m, we silently clamp to avoid
     * overlap with the MMIO region.  A proper high-memory (>4 GiB)
     * implementation would require an additional DDR region above
     * 0x1_0000_0000, which is not modeled here.
     */
    if (machine->ram_size > RK3399_RAM_MAX_LOW) {
        machine->ram_size = RK3399_RAM_MAX_LOW;
    }

    boot_info = (struct arm_boot_info) {
        .loader_start = RK3399_INTMEM0_BASE,
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
    mc->default_ram_size = 2 * GiB;
    mc->default_ram_id = "rk3399.ram";
}

DEFINE_MACHINE_AARCH64("rk3399", rk3399_machine_class_init)
