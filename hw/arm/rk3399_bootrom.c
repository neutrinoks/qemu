/*
 * Rockchip RK3399 BootROM Emulation.
 *
 * Emulates the hardware boot ROM sequence in C:
 *
 *   1. Probe eMMC image (if provided via -drive if=none,id=emmc)
 *   2. Probe SD image   (if provided via -drive if=none,id=sd)
 *   3. For each medium, search for a valid ID block at the well-known
 *      sector offsets, RC4-decrypt it, and check the magic.
 *   4. Parse the header to find the 1st stage (TPL) location and size.
 *   5. Load the TPL into INTMEM0 SRAM.
 *   6. Optionally RC4-decrypt the TPL image if the header says so.
 *   7. Set the CPU's program counter to the SRAM entry point.
 *
 * If no valid boot medium is found, the caller falls back to the
 * -kernel boot path (i.e. QEMU's built-in ELF/binary loader).
 *
 * The eMMC and SD images are raw block images passed via:
 *   -drive if=none,format=raw,id=emmc,file=emmc.img
 *   -drive if=none,format=raw,id=sd,file=sd.img
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "exec/cpu-common.h"
#include "hw/arm/rk3399.h"
#include "hw/arm/rk3399_bootrom.h"
#include "hw/core/boards.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "target/arm/cpu.h"

/* ------------------------------------------------------------------ */
/*  RC4 stream cipher                                                  */
/* ------------------------------------------------------------------ */

/*
 * Minimal RC4 implementation matching the Rockchip BootROM behaviour.
 * The BootROM uses standard RC4 with a 16-byte key for both the ID
 * block header and (optionally) the bootloader image payloads.
 */

typedef struct {
    uint8_t S[256];
    uint8_t i;
    uint8_t j;
} Rc4State;

static void rc4_init(Rc4State *rc4, const uint8_t *key, size_t key_len)
{
    int i;
    uint8_t j = 0;
    uint8_t tmp;

    for (i = 0; i < 256; i++) {
        rc4->S[i] = i;
    }

    for (i = 0; i < 256; i++) {
        j = j + rc4->S[i] + key[i % key_len];
        tmp = rc4->S[i];
        rc4->S[i] = rc4->S[j];
        rc4->S[j] = tmp;
    }

    rc4->i = 0;
    rc4->j = 0;
}

static void rc4_crypt(Rc4State *rc4, uint8_t *data, size_t len)
{
    size_t n;
    uint8_t tmp;

    for (n = 0; n < len; n++) {
        rc4->i += 1;
        rc4->j += rc4->S[rc4->i];

        tmp = rc4->S[rc4->i];
        rc4->S[rc4->i] = rc4->S[rc4->j];
        rc4->S[rc4->j] = tmp;

        data[n] ^= rc4->S[(uint8_t)(rc4->S[rc4->i] + rc4->S[rc4->j])];
    }
}

/* ------------------------------------------------------------------ */
/*  Block I/O helpers                                                  */
/* ------------------------------------------------------------------ */

/*
 * Read sectors from a raw block image file.
 *
 * @blk:     The BlockBackend for the drive.
 * @sector:  Starting sector number (512-byte sectors).
 * @buf:     Destination buffer (must be at least count * 512 bytes).
 * @count:   Number of sectors to read.
 *
 * Returns 0 on success, negative errno on failure.
 */
static int blk_read_sectors(BlockBackend *blk, uint64_t sector,
                            uint8_t *buf, uint32_t count)
{
    return blk_pread(blk, sector * RK_BLK_SIZE, count * RK_BLK_SIZE,
                     buf, 0);
}

/* ------------------------------------------------------------------ */
/*  ID block search + parse                                            */
/* ------------------------------------------------------------------ */

/*
 * Try to find and parse a valid Rockchip ID block on the given
 * BlockBackend.  Returns true if a valid block was found, filling
 * in *info with the boot parameters.
 */
static bool rk3399_search_idblock(BlockBackend *blk,
                                  Rk3399BootSource source,
                                  Rk3399BootInfo *info)
{
    uint8_t sector_buf[RK_BLK_SIZE];
    Rk3399IdBlockHeader *hdr = (Rk3399IdBlockHeader *)sector_buf;
    Rc4State rc4;
    int n;

    for (n = 0; n < RK_IDBLOCK_SEARCH_COUNT; n++) {
        uint64_t sector = RK_IDBLOCK_SEARCH_START
                        + (uint64_t)n * RK_IDBLOCK_SEARCH_STRIDE;
        int ret;

        /* Read the candidate sector */
        ret = blk_read_sectors(blk, sector, sector_buf, 1);
        if (ret < 0) {
            info_report("rk3399_bootrom:   sector %" PRIu64
                        ": read failed (err=%d)", sector, ret);
            continue;
        }

        /* Show first 8 raw bytes before decryption */
        info_report("rk3399_bootrom:   sector %" PRIu64
                    ": raw [%02x %02x %02x %02x %02x %02x %02x %02x]",
                    sector,
                    sector_buf[0], sector_buf[1],
                    sector_buf[2], sector_buf[3],
                    sector_buf[4], sector_buf[5],
                    sector_buf[6], sector_buf[7]);

        /* RC4-decrypt the entire 512-byte block */
        rc4_init(&rc4, rk3399_rc4_key, RK_RC4_KEY_LEN);
        rc4_crypt(&rc4, sector_buf, RK_BLK_SIZE);

        /* Show first 8 bytes after decryption + magic comparison */
        info_report("rk3399_bootrom:   sector %" PRIu64
                    ": dec [%02x %02x %02x %02x %02x %02x %02x %02x]"
                    " magic=0x%08x (expect 0x%08x)",
                    sector,
                    sector_buf[0], sector_buf[1],
                    sector_buf[2], sector_buf[3],
                    sector_buf[4], sector_buf[5],
                    sector_buf[6], sector_buf[7],
                    le32_to_cpu(hdr->magic), RK_IDBLOCK_MAGIC);

        /* Check magic */
        if (le32_to_cpu(hdr->magic) != RK_IDBLOCK_MAGIC) {
            continue;
        }

        /*
         * Valid ID block found.  Extract boot parameters.
         *
         * init_offset:   Sector offset of the 1st stage (TPL) relative
         *                to the ID block's sector position.
         *                U-Boot typically sets this to 4 (= 2 KiB after
         *                the header start).
         *
         * flash_data_sz: Size of the 1st stage in 2 KiB units.
         * flash_boot_sz: Size of the 2nd stage in 2 KiB units.
         */
        uint16_t init_off       = le16_to_cpu(hdr->init_offset);
        uint16_t init_sz        = le16_to_cpu(hdr->init_size);
        uint16_t init_boot_sz   = le16_to_cpu(hdr->init_boot_size);
        bool     no_rc4         = le32_to_cpu(hdr->disable_rc4) != 0;

        info->source            = source;
        info->images_rc4        = !no_rc4;
        info->init_offset       = sector + init_off;
        info->init_size         = (uint32_t)init_sz;
        info->init_boot_size    = (uint32_t)init_boot_sz;

        info_report("rk3399_bootrom: found ID block at sector %" PRIu64
                    " (source=%s, init_off=%u, init_sz=%u B, init_boot_sz=%u B,"
                    " rc4=%s)",
                    sector,
                    source == RK_BOOT_SRC_EMMC ? "eMMC" : "SD",
                    info->init_offset, info->init_size, info->init_boot_size,
                    info->images_rc4 ? "yes" : "no");

        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/*  Stage loader                                                       */
/* ------------------------------------------------------------------ */

/*
 * Load the 1st bootloader stage (TPL) from the boot medium into
 * INTMEM0 SRAM, optionally RC4-decrypting it.
 *
 * Returns true on success.
 */
static bool rk3399_load_first_stage(BlockBackend *blk,
                                    const Rk3399BootInfo *info)
{
    g_autofree uint8_t *buf = NULL;
    uint32_t sectors;
    uint32_t load_size;

    /*
     * Sanity-check: the 1st stage must fit in INTMEM0 (192 KiB).
     */
    if (info->init_size == 0 || info->init_size > RK3399_INTMEM0_SIZE) {
        error_report("rk3399_bootrom: 1st stage size %u exceeds INTMEM0 "
                     "(%d bytes)", info->init_size, RK3399_INTMEM0_SIZE);
        return false;
    }

    sectors = info->init_size * RK_SIZE_2BLK;
    load_size = sectors * RK_BLK_SIZE;
    buf = g_malloc(load_size);

    if (blk_read_sectors(blk, info->init_offset, buf, sectors) < 0) {
        error_report("rk3399_bootrom: failed to read 1st stage "
                     "(%u sectors at offset %u)",
                     sectors, info->init_offset);
        return false;
    }

    /* RC4-decrypt the image if the ID block header says so */
    if (info->images_rc4) {
        Rc4State rc4;
        rc4_init(&rc4, rk3399_rc4_key, RK_RC4_KEY_LEN);
        rc4_crypt(&rc4, buf, load_size);
    }

    /* Write into INTMEM0 via the system address space */
    cpu_physical_memory_write(RK3399_INTMEM0_BASE, buf, load_size);

    info_report("rk3399_bootrom: loaded 1st stage (%u bytes) to 0x%08x",
                load_size, RK3399_INTMEM0_BASE);

    return true;
}

/* ------------------------------------------------------------------ */
/*  Drive lookup helper                                                */
/* ------------------------------------------------------------------ */

/*
 * Find and claim a BlockBackend by its drive ID string.
 *
 * The user provides the drive via:
 *   -drive if=none,format=raw,id=<drive_id>,file=...
 *
 * With if=none, QEMU creates a BlockBackend but does not attach it
 * to any bus controller.  We look it up by name and "claim" it by
 * obtaining read permission, which also prevents QEMU from
 * complaining about orphaned drives at startup.
 *
 * Returns the BlockBackend on success, NULL if not found.
 */
static BlockBackend *rk3399_find_drive(const char *drive_id)
{
    BlockBackend *blk;
    DriveInfo *dinfo;
    int ret;

    blk = blk_by_name(drive_id);
    if (!blk || !blk_is_available(blk)) {
        return NULL;
    }

    /*
     * Request read permission on the drive.  Without this, blk_pread()
     * will fail because the BlockBackend has no permissions set when
     * created via -drive if=none.
     */
    ret = blk_set_perm(blk, BLK_PERM_CONSISTENT_READ, BLK_PERM_ALL,
                       &error_fatal);
    if (ret < 0) {
        return NULL;
    }

    /*
     * Mark the drive as claimed so QEMU's drive_check_orphaned()
     * does not abort with "machine type does not support if=none".
     * We do this by looking up the legacy DriveInfo and setting
     * is_default, or by simply noting that blk_by_name found it
     * (drives with if=none and type IF_NONE are excluded from the
     * orphan check by default in modern QEMU).
     */
    dinfo = blk_legacy_dinfo(blk);
    if (dinfo) {
        /*
         * IF_NONE drives are NOT checked by drive_check_orphaned()
         * in modern QEMU (the check explicitly skips dinfo->type ==
         * IF_NONE).  So we should be fine.  This note is here for
         * documentation.
         */
        (void)dinfo;
    }

    return blk;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

bool rk3399_bootrom_load(ARMCPU *cpu, MachineState *machine)
{
    /*
     * Boot medium probe order (matching real hardware):
     *   1. eMMC  (-drive if=none,id=emmc,file=...)
     *   2. SD    (-drive if=none,id=sd,file=...)
     *
     * SPI NOR/NAND is not yet implemented.
     */
    static const struct {
        const char     *drive_id;
        Rk3399BootSource source;
    } probe_order[] = {
        { "emmc", RK_BOOT_SRC_EMMC },
        { "sd",   RK_BOOT_SRC_SD   },
    };

    Rk3399BootInfo info = { .source = RK_BOOT_SRC_NONE };
    BlockBackend *blk = NULL;
    int i;

    for (i = 0; i < ARRAY_SIZE(probe_order); i++) {
        BlockBackend *candidate = rk3399_find_drive(probe_order[i].drive_id);
        if (!candidate) {
            continue;
        }

        info_report("rk3399_bootrom: probing %s for ID block...",
                    probe_order[i].drive_id);

        if (rk3399_search_idblock(candidate, probe_order[i].source, &info)) {
            blk = candidate;
            break;
        }
    }

    if (!blk) {
        /* No bootable medium found — caller should fall back to -kernel */
        return false;
    }

    /* Load the 1st stage (TPL) into INTMEM0 SRAM */
    if (!rk3399_load_first_stage(blk, &info)) {
        return false;
    }

    /*
     * Set the CPU's program counter to the INTMEM0 entry point.
     *
     * On the real RK3399, the BootROM code jumps to the SRAM load
     * address after loading the TPL.  We emulate this by setting
     * the CPU's PC directly.
     */
    cpu_set_pc(CPU(cpu), RK3399_INTMEM0_BASE);

    return true;
}
