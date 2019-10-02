/*
 * ASPEED SoC family
 *
 * Andrew Jeffery <andrew@aj.id.au>
 *
 * Copyright 2016 IBM Corp.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#ifndef ASPEED_SOC_H
#define ASPEED_SOC_H

#include "hw/intc/aspeed_vic.h"
#include "hw/misc/aspeed_scu.h"
#include "hw/misc/aspeed_sdmc.h"
#include "hw/misc/aspeed_xdma.h"
#include "hw/timer/aspeed_timer.h"
#include "hw/timer/aspeed_rtc.h"
#include "hw/i2c/aspeed_i2c.h"
#include "hw/ssi/aspeed_smc.h"
#include "hw/watchdog/wdt_aspeed.h"
#include "hw/net/ftgmac100.h"

#define ASPEED_SPIS_NUM  2
#define ASPEED_WDTS_NUM  3
#define ASPEED_CPUS_NUM  2
#define ASPEED_MACS_NUM  2

typedef struct AspeedSoCState {
    /*< private >*/
    DeviceState parent;

    /*< public >*/
    ARMCPU cpu[ASPEED_CPUS_NUM];
    uint32_t num_cpus;
    MemoryRegion sram;
    AspeedVICState vic;
    AspeedRtcState rtc;
    AspeedTimerCtrlState timerctrl;
    AspeedI2CState i2c;
    AspeedSCUState scu;
    AspeedXDMAState xdma;
    AspeedSMCState fmc;
    AspeedSMCState spi[ASPEED_SPIS_NUM];
    AspeedSDMCState sdmc;
    AspeedWDTState wdt[ASPEED_WDTS_NUM];
    FTGMAC100State ftgmac100[ASPEED_MACS_NUM];
} AspeedSoCState;

#define TYPE_ASPEED_SOC "aspeed-soc"
#define ASPEED_SOC(obj) OBJECT_CHECK(AspeedSoCState, (obj), TYPE_ASPEED_SOC)

typedef struct AspeedSoCInfo {
    const char *name;
    const char *cpu_type;
    uint32_t silicon_rev;
    uint64_t sram_size;
    int spis_num;
    const char *fmc_typename;
    const char **spi_typename;
    int wdts_num;
    const int *irqmap;
    const hwaddr *memmap;
    uint32_t num_cpus;
} AspeedSoCInfo;

typedef struct AspeedSoCClass {
    DeviceClass parent_class;
    AspeedSoCInfo *info;
} AspeedSoCClass;

#define ASPEED_SOC_CLASS(klass)                                         \
    OBJECT_CLASS_CHECK(AspeedSoCClass, (klass), TYPE_ASPEED_SOC)
#define ASPEED_SOC_GET_CLASS(obj)                               \
    OBJECT_GET_CLASS(AspeedSoCClass, (obj), TYPE_ASPEED_SOC)

enum {
    ASPEED_IOMEM,
    ASPEED_UART1,
    ASPEED_UART2,
    ASPEED_UART3,
    ASPEED_UART4,
    ASPEED_UART5,
    ASPEED_VUART,
    ASPEED_FMC,
    ASPEED_SPI1,
    ASPEED_SPI2,
    ASPEED_VIC,
    ASPEED_SDMC,
    ASPEED_SCU,
    ASPEED_ADC,
    ASPEED_SRAM,
    ASPEED_GPIO,
    ASPEED_RTC,
    ASPEED_TIMER1,
    ASPEED_TIMER2,
    ASPEED_TIMER3,
    ASPEED_TIMER4,
    ASPEED_TIMER5,
    ASPEED_TIMER6,
    ASPEED_TIMER7,
    ASPEED_TIMER8,
    ASPEED_WDT,
    ASPEED_PWM,
    ASPEED_LPC,
    ASPEED_IBT,
    ASPEED_I2C,
    ASPEED_ETH1,
    ASPEED_ETH2,
    ASPEED_SDRAM,
    ASPEED_XDMA,
};

#endif /* ASPEED_SOC_H */
