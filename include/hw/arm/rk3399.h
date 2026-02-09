/*
 * Rockchip RK3399 SoC.
 *
 * Copyright (c) 2026 Neutrinoks <mail@neutrinoks.io>.
 *
 */

#ifndef HW_ARM_RK3399_SOC_H
#define HW_ARM_RK3399_SOC_H

#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "target/arm/cpu.h"
#include "target/arm/cpu-qom.h"

#define TYPE_RK3399 "rk3399"

OBJECT_DECLARE_SIMPLE_TYPE(Rk3399State, RK3399)

#define RK3399_NUM_CPUS 1
#define RK3399_RAM_START 0x00000000

struct Rk3399State {
    SysBusDevice parent_obj;

    ARMCPU cpu;
};

#endif
