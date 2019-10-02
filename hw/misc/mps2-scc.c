/*
 * ARM MPS2 SCC emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/* This is a model of the SCC (Serial Communication Controller)
 * found in the FPGA images of MPS2 development boards.
 *
 * Documentation of it can be found in the MPS2 TRM:
 * http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.100112_0100_03_en/index.html
 * and also in the Application Notes documenting individual FPGA images.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/mps2-scc.h"

REG32(CFG0, 0)
REG32(CFG1, 4)
REG32(CFG3, 0xc)
REG32(CFG4, 0x10)
REG32(CFGDATA_RTN, 0xa0)
REG32(CFGDATA_OUT, 0xa4)
REG32(CFGCTRL, 0xa8)
    FIELD(CFGCTRL, DEVICE, 0, 12)
    FIELD(CFGCTRL, RES1, 12, 8)
    FIELD(CFGCTRL, FUNCTION, 20, 6)
    FIELD(CFGCTRL, RES2, 26, 4)
    FIELD(CFGCTRL, WRITE, 30, 1)
    FIELD(CFGCTRL, START, 31, 1)
REG32(CFGSTAT, 0xac)
    FIELD(CFGSTAT, DONE, 0, 1)
    FIELD(CFGSTAT, ERROR, 1, 1)
REG32(DLL, 0x100)
REG32(AID, 0xFF8)
REG32(ID, 0xFFC)

/* Handle a write via the SYS_CFG channel to the specified function/device.
 * Return false on error (reported to guest via SYS_CFGCTRL ERROR bit).
 */
static bool scc_cfg_write(MPS2SCC *s, unsigned function,
                          unsigned device, uint32_t value)
{
    trace_mps2_scc_cfg_write(function, device, value);

    if (function != 1 || device >= NUM_OSCCLK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC config write: bad function %d device %d\n",
                      function, device);
        return false;
    }

    s->oscclk[device] = value;
    return true;
}

/* Handle a read via the SYS_CFG channel to the specified function/device.
 * Return false on error (reported to guest via SYS_CFGCTRL ERROR bit),
 * or set *value on success.
 */
static bool scc_cfg_read(MPS2SCC *s, unsigned function,
                         unsigned device, uint32_t *value)
{
    if (function != 1 || device >= NUM_OSCCLK) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC config read: bad function %d device %d\n",
                      function, device);
        return false;
    }

    *value = s->oscclk[device];

    trace_mps2_scc_cfg_read(function, device, *value);
    return true;
}

static uint64_t mps2_scc_read(void *opaque, hwaddr offset, unsigned size)
{
    MPS2SCC *s = MPS2_SCC(opaque);
    uint64_t r;

    switch (offset) {
    case A_CFG0:
        r = s->cfg0;
        break;
    case A_CFG1:
        r = s->cfg1;
        break;
    case A_CFG3:
        /* These are user-settable DIP switches on the board. We don't
         * model that, so just return zeroes.
         */
        r = 0;
        break;
    case A_CFG4:
        r = s->cfg4;
        break;
    case A_CFGDATA_RTN:
        r = s->cfgdata_rtn;
        break;
    case A_CFGDATA_OUT:
        r = s->cfgdata_out;
        break;
    case A_CFGCTRL:
        r = s->cfgctrl;
        break;
    case A_CFGSTAT:
        r = s->cfgstat;
        break;
    case A_DLL:
        r = s->dll;
        break;
    case A_AID:
        r = s->aid;
        break;
    case A_ID:
        r = s->id;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC read: bad offset %x\n", (int) offset);
        r = 0;
        break;
    }

    trace_mps2_scc_read(offset, r, size);
    return r;
}

static void mps2_scc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    MPS2SCC *s = MPS2_SCC(opaque);

    trace_mps2_scc_write(offset, value, size);

    switch (offset) {
    case A_CFG0:
        /* TODO on some boards bit 0 controls RAM remapping */
        s->cfg0 = value;
        break;
    case A_CFG1:
        /* CFG1 bits [7:0] control the board LEDs. We don't currently have
         * a mechanism for displaying this graphically, so use a trace event.
         */
        trace_mps2_scc_leds(value & 0x80 ? '*' : '.',
                            value & 0x40 ? '*' : '.',
                            value & 0x20 ? '*' : '.',
                            value & 0x10 ? '*' : '.',
                            value & 0x08 ? '*' : '.',
                            value & 0x04 ? '*' : '.',
                            value & 0x02 ? '*' : '.',
                            value & 0x01 ? '*' : '.');
        s->cfg1 = value;
        break;
    case A_CFGDATA_OUT:
        s->cfgdata_out = value;
        break;
    case A_CFGCTRL:
        /* Writing to CFGCTRL clears SYS_CFGSTAT */
        s->cfgstat = 0;
        s->cfgctrl = value & ~(R_CFGCTRL_RES1_MASK |
                               R_CFGCTRL_RES2_MASK |
                               R_CFGCTRL_START_MASK);

        if (value & R_CFGCTRL_START_MASK) {
            /* Start bit set -- do a read or write (instantaneously) */
            int device = extract32(s->cfgctrl, R_CFGCTRL_DEVICE_SHIFT,
                                   R_CFGCTRL_DEVICE_LENGTH);
            int function = extract32(s->cfgctrl, R_CFGCTRL_FUNCTION_SHIFT,
                                     R_CFGCTRL_FUNCTION_LENGTH);

            s->cfgstat = R_CFGSTAT_DONE_MASK;
            if (s->cfgctrl & R_CFGCTRL_WRITE_MASK) {
                if (!scc_cfg_write(s, function, device, s->cfgdata_out)) {
                    s->cfgstat |= R_CFGSTAT_ERROR_MASK;
                }
            } else {
                uint32_t result;
                if (!scc_cfg_read(s, function, device, &result)) {
                    s->cfgstat |= R_CFGSTAT_ERROR_MASK;
                } else {
                    s->cfgdata_rtn = result;
                }
            }
        }
        break;
    case A_DLL:
        /* DLL stands for Digital Locked Loop.
         * Bits [31:24] (DLL_LOCK_MASK) are writable, and indicate a
         * mask of which of the DLL_LOCKED bits [16:23] should be ORed
         * together to determine the ALL_UNMASKED_DLLS_LOCKED bit [0].
         * For QEMU, our DLLs are always locked, so we can leave bit 0
         * as 1 always and don't need to recalculate it.
         */
        s->dll = deposit32(s->dll, 24, 8, extract32(value, 24, 8));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "MPS2 SCC write: bad offset 0x%x\n", (int) offset);
        break;
    }
}

static const MemoryRegionOps mps2_scc_ops = {
    .read = mps2_scc_read,
    .write = mps2_scc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void mps2_scc_reset(DeviceState *dev)
{
    MPS2SCC *s = MPS2_SCC(dev);
    int i;

    trace_mps2_scc_reset();
    s->cfg0 = 0;
    s->cfg1 = 0;
    s->cfgdata_rtn = 0;
    s->cfgdata_out = 0;
    s->cfgctrl = 0x100000;
    s->cfgstat = 0;
    s->dll = 0xffff0001;
    for (i = 0; i < NUM_OSCCLK; i++) {
        s->oscclk[i] = s->oscclk_reset[i];
    }
}

static void mps2_scc_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MPS2SCC *s = MPS2_SCC(obj);

    memory_region_init_io(&s->iomem, obj, &mps2_scc_ops, s, "mps2-scc", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void mps2_scc_realize(DeviceState *dev, Error **errp)
{
}

static const VMStateDescription mps2_scc_vmstate = {
    .name = "mps2-scc",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cfg0, MPS2SCC),
        VMSTATE_UINT32(cfg1, MPS2SCC),
        VMSTATE_UINT32(cfgdata_rtn, MPS2SCC),
        VMSTATE_UINT32(cfgdata_out, MPS2SCC),
        VMSTATE_UINT32(cfgctrl, MPS2SCC),
        VMSTATE_UINT32(cfgstat, MPS2SCC),
        VMSTATE_UINT32(dll, MPS2SCC),
        VMSTATE_UINT32_ARRAY(oscclk, MPS2SCC, NUM_OSCCLK),
        VMSTATE_END_OF_LIST()
    }
};

static Property mps2_scc_properties[] = {
    /* Values for various read-only ID registers (which are specific
     * to the board model or FPGA image)
     */
    DEFINE_PROP_UINT32("scc-cfg4", MPS2SCC, cfg4, 0),
    DEFINE_PROP_UINT32("scc-aid", MPS2SCC, aid, 0),
    DEFINE_PROP_UINT32("scc-id", MPS2SCC, id, 0),
    /* These are the initial settings for the source clocks on the board.
     * In hardware they can be configured via a config file read by the
     * motherboard configuration controller to suit the FPGA image.
     * These default values are used by most of the standard FPGA images.
     */
    DEFINE_PROP_UINT32("oscclk0", MPS2SCC, oscclk_reset[0], 50000000),
    DEFINE_PROP_UINT32("oscclk1", MPS2SCC, oscclk_reset[1], 24576000),
    DEFINE_PROP_UINT32("oscclk2", MPS2SCC, oscclk_reset[2], 25000000),
    DEFINE_PROP_END_OF_LIST(),
};

static void mps2_scc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = mps2_scc_realize;
    dc->vmsd = &mps2_scc_vmstate;
    dc->reset = mps2_scc_reset;
    dc->props = mps2_scc_properties;
}

static const TypeInfo mps2_scc_info = {
    .name = TYPE_MPS2_SCC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MPS2SCC),
    .instance_init = mps2_scc_init,
    .class_init = mps2_scc_class_init,
};

static void mps2_scc_register_types(void)
{
    type_register_static(&mps2_scc_info);
}

type_init(mps2_scc_register_types);
