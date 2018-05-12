/*
 * Copyright (C) 2006 InnoTek Systemberatung GmbH
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation,
 * in version 2 as it comes in the "COPYING" file of the VirtualBox OSE
 * distribution. VirtualBox OSE is distributed in the hope that it will
 * be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * If you received this file as part of a commercial VirtualBox
 * distribution, then only the terms of your commercial VirtualBox
 * license agreement apply instead of the previous paragraph.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#ifndef HW_AC97_INT_H
#define HW_AC97_INT_H

#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/audio/soundhw.h"
#include "audio/audio.h"
#include "hw/pci/pci.h"
#include "sysemu/dma.h"

typedef struct BD {
    uint32_t addr;
    uint32_t ctl_len;
} BD;

typedef struct AC97BusMasterRegs {
    uint32_t bdbar;             /* rw 0 */
    uint8_t civ;                /* ro 0 */
    uint8_t lvi;                /* rw 0 */
    uint16_t sr;                /* rw 1 */
    uint16_t picb;              /* ro 0 */
    uint8_t piv;                /* ro 0 */
    uint8_t cr;                 /* rw 0 */
    unsigned int bd_valid;
    BD bd;
} AC97BusMasterRegs;

typedef struct AC97LinkState {
    PCIDevice *dev;
    QEMUSoundCard card;
    uint32_t use_broken_id;
    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint32_t cas;
    uint32_t last_samp;
    AC97BusMasterRegs bm_regs[3];
    uint8_t mixer_data[256];
    SWVoiceIn *voice_pi;
    SWVoiceOut *voice_po;
    SWVoiceIn *voice_mc;
    int invalid_freq[3];
    uint8_t silence[128];
    int bup_flag;
    MemoryRegion io_nam;
    MemoryRegion io_nabm;
} AC97LinkState;

extern const MemoryRegionOps ac97_io_nam_ops;
extern const MemoryRegionOps ac97_io_nabm_ops;

void ac97_common_init(AC97LinkState *s, PCIDevice *dev);

#endif
