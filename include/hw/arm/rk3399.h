/*
 * Rockchip RK3399 SoC.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#ifndef HW_ARM_RK3399_SOC_H
#define HW_ARM_RK3399_SOC_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "hw/intc/arm_gicv3_common.h"
#include "hw/misc/unimp.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"
#include "hw/arm/rk3399_cru.h"
#include "hw/arm/rk3399_grf.h"
#include "hw/arm/rk3399_ddr.h"

#define TYPE_RK3399 "rk3399"

OBJECT_DECLARE_SIMPLE_TYPE(Rk3399State, RK3399)

#define RK3399_NUM_CPUS             1

/*
 * DDR Memory Layout
 *
 * The RK3399 maps external DDR starting at physical address 0x0000_0000.
 * The usable low DDR region extends up to the MMIO aperture which begins
 * at 0xF800_0000 (the upper 128 MiB of the 32-bit address space is
 * reserved for peripheral registers, SRAM, BootROM, GIC, etc.).
 *
 * Boards with >3968 MiB RAM would need a high memory region above 4 GiB,
 * which is not modeled here.
 *
 * For QEMU, we use a single contiguous low-DDR region.  The machine's
 * default_ram_size is clamped to RK3399_RAM_MAX_LOW to prevent overlap
 * with the MMIO region.
 */
#define RK3399_RAM_START            0x00000000
#define RK3399_MMIO_START           0xF8000000ULL
#define RK3399_RAM_MAX_LOW          (RK3399_MMIO_START - RK3399_RAM_START)

/*
 * Embedded SRAM (on-chip, always available)
 *
 * These are the two internal memory blocks accessible by the Cortex-A
 * cores.  They are used by BootROM, TF-A (BL31), and for early boot
 * code before DDR is initialized.
 *
 * From TRM Chapter 7 and the address map (Fig. 1-1):
 *
 *   INTMEM0: 192 KiB @ 0xFF8C_0000  (main SRAM, perilp power domain)
 *            Used by BootROM to load TPL/SPL, by TF-A for BL31 SRAM
 *            sections, and by the Cortex-M0 for DDR frequency scaling.
 *
 *   INTMEM1:  64 KiB @ 0xFF3B_0000  (secondary SRAM, perilp power domain)
 *            Available as general-purpose SRAM after boot.
 */
#define RK3399_INTMEM0_BASE         0xFF8C0000
#define RK3399_INTMEM0_SIZE         (192 * 1024)

#define RK3399_INTMEM1_BASE         0xFF3B0000
#define RK3399_INTMEM1_SIZE         (64 * 1024)

/*
 * GIC-500 memory map (GICv3)
 *
 * The RK3399 integrates an ARM GIC-500 implementing GICv3.
 * GICv3 uses system registers (ICC_*_EL1) for the CPU interface
 * instead of memory-mapped GICC/GICH/GICV regions.
 * Only the Distributor (GICD) and Redistributors (GICR) are MMIO.
 *
 * From TRM:
 *   GICD: 0xFEE0_0000  (64 KiB)
 *   GICR: 0xFEF0_0000  (128 KiB per CPU: 2 x 64 KiB frames)
 */
#define RK3399_GIC_DIST_BASE        0xFEE00000
#define RK3399_GIC_DIST_SIZE        0x00010000
#define RK3399_GIC_REDIST_BASE      0xFEF00000
#define RK3399_GIC_REDIST_SIZE      0x00020000

/*
 * Number of SPI lines.
 * RK3399 TRM documents 148 SPIs; rounded up to the next multiple of 32
 * as required by GIC architecture (GICD_TYPER.ITLinesNumber).
 * 160 SPIs -> ITLinesNumber = 4, covering SPI 0..159 (INTID 32..191).
 */
#define RK3399_GIC_NUM_SPI          160

/* Per Processor Interrupts (PPI INTID offsets within GIC_INTERNAL) */
#define RK3399_GIC_PPI_MAINT        9
#define RK3399_GIC_PPI_HYPTIMER     10
#define RK3399_GIC_PPI_VIRTTIMER    11
#define RK3399_GIC_PPI_SECTIMER     13
#define RK3399_GIC_PPI_PHYSTIMER    14

/*
 * Shared Peripheral Interrupts — UARTs
 *
 * Values are SPI numbers (QEMU GPIO input index).
 * ARM INTID = SPI + 32.
 *
 * From Linux DTS (rk3399.dtsi):
 *   UART0  SPI  99  (INTID 131)   @ 0xFF18_0000  perilp, Bluetooth/flow-ctrl
 *   UART1  SPI  98  (INTID 130)   @ 0xFF19_0000  perilp, general purpose
 *   UART2  SPI 100  (INTID 132)   @ 0xFF1A_0000  perilp, debug console
 *   UART3  SPI 101  (INTID 133)   @ 0xFF1B_0000  perilp, flow-ctrl capable
 *   UART4  SPI 102  (INTID 134)   @ 0xFF37_0000  PMU domain
 */
#define RK3399_GIC_SPI_UART0        99
#define RK3399_GIC_SPI_UART1        98
#define RK3399_GIC_SPI_UART2        100
#define RK3399_GIC_SPI_UART3        101
#define RK3399_GIC_SPI_UART4        102

/*
 * UART base addresses and hardware parameters
 *
 * All five UARTs are Synopsys DesignWare APB UARTs (snps,dw-apb-uart),
 * 16550-compatible.  Each occupies a 256-byte (0x100) MMIO aperture
 * with 32-bit wide registers at 4-byte aligned offsets (reg-shift = 2).
 *
 * UART0..3 are clocked from the CRU (perilp bus).
 * UART4 is clocked from the PMU CRU (always-on domain).
 */
#define RK3399_NUM_UARTS            5

#define RK3399_UART0_BASE           0xFF180000
#define RK3399_UART1_BASE           0xFF190000
#define RK3399_UART2_BASE           0xFF1A0000
#define RK3399_UART3_BASE           0xFF1B0000
#define RK3399_UART4_BASE           0xFF370000

#define RK3399_UART_REG_SHIFT       2

struct Rk3399State {
    SysBusDevice parent_obj;

    ARMCPU cpu;
    GICv3State gic;

    MemoryRegion intmem0;
    MemoryRegion intmem1;

    Rk3399CruState cru;
    Rk3399PmuCruState pmucru;

    Rk3399GrfState grf;
    Rk3399GrfState pmugrf;
    Rk3399GrfState pmusgrf;

    Rk3399DdrState ddr;
};

#endif
