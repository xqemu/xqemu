/*
 * TI touchscreen controller
 *
 * Copyright (c) 2006 Andrzej Zaborowski
 * Copyright (C) 2008 Nokia Corporation
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_INPUT_TSC2XXX_H
#define HW_INPUT_TSC2XXX_H

#include "hw/irq.h"
#include "ui/console.h"

typedef struct uWireSlave {
    uint16_t (*receive)(void *opaque);
    void (*send)(void *opaque, uint16_t data);
    void *opaque;
} uWireSlave;

/* tsc210x.c */
uWireSlave *tsc2102_init(qemu_irq pint);
uWireSlave *tsc2301_init(qemu_irq penirq, qemu_irq kbirq, qemu_irq dav);
I2SCodec *tsc210x_codec(uWireSlave *chip);
uint32_t tsc210x_txrx(void *opaque, uint32_t value, int len);
void tsc210x_set_transform(uWireSlave *chip, MouseTransformInfo *info);
void tsc210x_key_event(uWireSlave *chip, int key, int down);

/* tsc2005.c */
void *tsc2005_init(qemu_irq pintdav);
uint32_t tsc2005_txrx(void *opaque, uint32_t value, int len);
void tsc2005_set_transform(void *opaque, MouseTransformInfo *info);

#endif
