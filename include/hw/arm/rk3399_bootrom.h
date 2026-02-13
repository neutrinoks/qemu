/*
 * Rockchip RK3399 BootROM Emulation.
 *
 * Emulates the hardware boot ROM sequence: probing eMMC and SD for a
 * valid Rockchip ID block, RC4-decrypting it, parsing the header, and
 * loading the first bootloader stage (TPL) into INTMEM0 SRAM.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#ifndef HW_ARM_RK3399_BOOTROM_H
#define HW_ARM_RK3399_BOOTROM_H

#include "qemu/osdep.h"

/*
 * Rockchip ID Block Header (V1)
 *
 * The ID block is a 512-byte sector at well-known locations on the boot
 * medium.  It is RC4-encrypted with a fixed key.  After decryption, the
 * first 4 bytes must match RK_IDBLOCK_MAGIC.
 *
 * Layout (all fields little-endian):
 *
 *   Offset  Size  Field
 *   0x000   4B    magic           Must be 0x0FF0AA55 after decryption
 *   0x004   4B    reserved
 *   0x008   4B    disable_rc4     0 = bootloader images are also RC4-
 *                                     encrypted; 1 = plaintext images
 *   0x00C   2B    init_offset     Sector offset of 1st stage (TPL)
 *                                 relative to the start of the boot medium
 *   0x00E   2B    init_size       (V1: not directly used; see below)
 *   0x010   ...   reserved
 *   0x1FA   2B    flash_data_sz   Size of 1st stage in 2 KiB blocks
 *   0x1FC   2B    flash_boot_sz   Size of 2nd stage in 2 KiB blocks
 *   0x1FE   2B    reserved
 *
 * Reference: U-Boot tools/rkcommon.c (struct header0_info)
 */

#define RK_IDBLOCK_MAGIC        0x0FF0AA55u
#define RK_BLK_SIZE             512
#define RK_SIZE_2BLK            4 /* 2 KiB in 512-byte sectors */

/*
 * Packed header matching the on-disk layout.
 * All fields are stored little-endian on the medium; we byteswap
 * after RC4 decryption.
 */
typedef struct QEMU_PACKED Rk3399IdBlockHeader {
    uint32_t    magic;              /* 0x000 */
    uint8_t     reserved0[4];       /* 0x004 */
    uint32_t    disable_rc4;        /* 0x008 */
    uint16_t    init_offset;        /* 0x00C: sector offset of 1st stage */
    uint8_t     reserved1[492];     /* 0x010..0x1F9 */
    uint16_t    init_size;          /* 0x1FA: 1st stage size (2 KiB units) */
    uint16_t    init_boot_size;     /* 0x1FC: Total size, 1st & 2nd (2 KiB units) */
    uint8_t     reserved2[2];       /* 0x1FE */
} Rk3399IdBlockHeader;

QEMU_BUILD_BUG_ON(sizeof(Rk3399IdBlockHeader) != RK_BLK_SIZE);

/*
 * RC4 key used by the RK3399 BootROM to decrypt the ID block (and
 * optionally the bootloader images).  This is a well-known, fixed key
 * documented in U-Boot tools/rkcommon.c and multiple public sources.
 *
 * Hex: 7c 4e 03 04 55 05 09 07 2d 2c 7b 38 17 0d 17 11
 */
#define RK_RC4_KEY_LEN  16

static const uint8_t rk3399_rc4_key[RK_RC4_KEY_LEN] = {
    124, 78, 3, 4, 85, 5, 9, 7, 45, 44, 123, 56, 23, 13, 23, 17
};

/*
 * eMMC/SD ID block search positions.
 *
 * The BootROM searches for the ID block at sector (64 + 1024*n) for
 * n = 0..4, i.e. sectors { 64, 1088, 2112, 3136, 4160 }.
 */
#define RK_IDBLOCK_SEARCH_COUNT     5
#define RK_IDBLOCK_SEARCH_START     64      /* first candidate sector */
#define RK_IDBLOCK_SEARCH_STRIDE    1024    /* sector stride */

/*
 * Boot source identifiers (matching the real BootROM probe order).
 * SPI is not yet implemented.
 */
typedef enum {
    RK_BOOT_SRC_EMMC = 0,
    RK_BOOT_SRC_SD,
    RK_BOOT_SRC_NONE,  /* no bootable medium found (→ MaskROM / -kernel) */
} Rk3399BootSource;

/*
 * Result of the BootROM ID block search + load operation.
 */
typedef struct {
    Rk3399BootSource    source;
    bool                images_rc4;         /* true if bootloader images are
                                             * also RC4-encrypted */
    uint32_t            init_offset;        /* absolute sector of 1st stage */
    uint32_t            init_size;          /* 1st stage size in bytes */
    uint32_t            init_boot_size;     /* 2nd stage size in bytes */
} Rk3399BootInfo;

/* Forward declaration — implemented in rk3399_bootrom.c */
struct MachineState;
struct ARMCPU;

/**
 * rk3399_bootrom_load() - Emulate the BootROM boot sequence.
 *
 * Probes eMMC and SD images (provided via -drive) for a valid Rockchip
 * ID block, decrypts and parses it, and loads the first bootloader
 * stage (TPL) into INTMEM0 SRAM.  Sets the CPU reset vector to the
 * SRAM entry point.
 *
 * If no valid ID block is found on any medium, the function returns
 * false and the caller should fall back to the existing -kernel path.
 *
 * @cpu:     The boot CPU (Cortex-A53 CPU0).
 * @machine: The machine state (for accessing -drive options).
 *
 * Returns true if a bootloader was successfully loaded.
 */
bool rk3399_bootrom_load(ARMCPU *cpu, MachineState *machine);

#endif /* HW_ARM_RK3399_BOOTROM_H */
