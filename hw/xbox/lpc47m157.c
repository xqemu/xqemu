/*
 * QEMU SMSC LPC47M157 (Super I/O)
 *
 * Copyright (c) 2013 espes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/char/serial.h"
#include "hw/isa/isa.h"
#include "qapi/error.h"

#define MAX_DEVICE 0xC
#define DEVICE_FDD              0x0
#define DEVICE_PARALLEL_PORT    0x3
#define DEVICE_SERIAL_PORT_1    0x4
#define DEVICE_SERIAL_PORT_2    0x5
#define DEVICE_KEYBOARD         0x7
#define DEVICE_GAME_PORT        0x9
#define DEVICE_PME              0xA
#define DEVICE_MPU_401          0xB

#define ENTER_CONFIG_KEY    0x55
#define EXIT_CONFIG_KEY     0xAA

#define MAX_CONFIG_REG  0x30
#define MAX_DEVICE_REGS 0xFF

#define CONFIG_DEVICE_NUMBER    0x07
#define CONFIG_PORT_LOW         0x26
#define CONFIG_PORT_HIGH        0x27

#define CONFIG_DEVICE_ACTIVATE              0x30
#define CONFIG_DEVICE_BASE_ADDRESS_HIGH     0x60
#define CONFIG_DEVICE_BASE_ADDRESS_LOW      0x61
#define CONFIG_DEVICE_INTERRUPT             0x70

// #define DEBUG
#ifdef DEBUG
# define DPRINTF(format, ...) printf(format, ## __VA_ARGS__)
#else
# define DPRINTF(format, ...) do { } while (0)
#endif

typedef struct LPC47M157State {
    ISADevice dev;

    MemoryRegion io;

    bool configuration_mode;
    uint32_t selected_reg;

    uint8_t config_regs[MAX_CONFIG_REG];
    uint8_t device_regs[MAX_DEVICE][MAX_DEVICE_REGS];

    struct {
        bool active;
        SerialState state;
    } serial[2];
} LPC47M157State;

#define LPC47M157_DEVICE(obj) \
    OBJECT_CHECK(LPC47M157State, (obj), "lpc47m157")

static void update_devices(LPC47M157State *s)
{
    ISADevice *isadev = ISA_DEVICE(s);
    int i;

    /* init serial devices */
    for (i = 0; i < 2; i++) {
        uint8_t *dev = s->device_regs[DEVICE_SERIAL_PORT_1 + i];
        if (dev[CONFIG_DEVICE_ACTIVATE] && !s->serial[i].active) {
            uint32_t iobase = (dev[CONFIG_DEVICE_BASE_ADDRESS_HIGH] << 8)
                                | dev[CONFIG_DEVICE_BASE_ADDRESS_LOW];
            uint32_t irq = dev[CONFIG_DEVICE_INTERRUPT];

            SerialState *ss = &s->serial[i].state;
            if (irq != 0) {
                isa_init_irq(isadev, &ss->irq, irq);
            }
            isa_register_ioport(isadev, &ss->io, iobase);

            s->serial[i].active = true;
        }
    }
}

static void lpc47m157_io_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned int size)
{
    LPC47M157State *s = opaque;

    DPRINTF("lpc47m157 io write 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);

    if (addr == 0) {
        /* INDEX_PORT */
        if (val == ENTER_CONFIG_KEY) {
            if (s->configuration_mode) {
                printf("lpc47m157 io write: Attempted to reenter configuration mode\n");
            }
            DPRINTF("lpc47m157 io write: Entering configuration mode\n");

            s->configuration_mode = true;
        } else if (val == EXIT_CONFIG_KEY) {
            if (!s->configuration_mode) {
                printf("lpc47m157 io write: Attempted to reexit configuration mode\n");
            }
            DPRINTF("lpc47m157 io write: Exiting configuration mode\n");

            s->configuration_mode = false;

            update_devices(s);
        } else {
            s->selected_reg = val;
        }
    } else if (addr == 1) {
        /* DATA_PORT */
        if (s->selected_reg < MAX_CONFIG_REG) {
            /* global configuration register */
            s->config_regs[s->selected_reg] = val;
        } else {
            /* device register */
            assert(s->config_regs[CONFIG_DEVICE_NUMBER] < MAX_DEVICE);
            uint8_t *dev = s->device_regs[s->config_regs[CONFIG_DEVICE_NUMBER]];
            dev[s->selected_reg] = val;
            DPRINTF("lpc47m157 dev %x . %x = %"PRIx64"\n",
                s->config_regs[CONFIG_DEVICE_NUMBER],
                s->selected_reg, val);
        }
    } else {
        assert(false);
    }
}

static uint64_t lpc47m157_io_read(void *opaque, hwaddr addr, unsigned int size)
{
    LPC47M157State *s = opaque;
    uint32_t val = 0;

    if (addr == 0) {
        /* INDEX_PORT */
    } else if (addr == 1) {
        /* DATA_PORT */
        if (s->selected_reg < MAX_CONFIG_REG) {
            val = s->config_regs[s->selected_reg];
        } else {
            assert(s->config_regs[CONFIG_DEVICE_NUMBER] < MAX_DEVICE);
            uint8_t *dev = s->device_regs[s->config_regs[CONFIG_DEVICE_NUMBER]];
            val = dev[s->selected_reg];
        }
    } else {
        assert(false);
    }

    DPRINTF("lpc47m157 io read 0x%"HWADDR_PRIx" -> 0x%x\n", addr, val);
    return val;
}

static const MemoryRegionOps lpc47m157_io_ops = {
    .read  = lpc47m157_io_read,
    .write = lpc47m157_io_write,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static Property lpc47m157_properties[] = {
    DEFINE_PROP_CHR("chardev0", LPC47M157State, serial[0].state.chr),
    DEFINE_PROP_CHR("chardev1", LPC47M157State, serial[1].state.chr),
    DEFINE_PROP_END_OF_LIST(),
};

static void lpc47m157_realize(DeviceState *dev, Error **errp)
{
    LPC47M157State *s = LPC47M157_DEVICE(dev);
    ISADevice *isa = ISA_DEVICE(dev);
    int i;

    const uint32_t iobase = 0x2e; //0x4e if SYSOPT pin, make it a property
    s->config_regs[CONFIG_PORT_LOW] = iobase & 0xFF;
    s->config_regs[CONFIG_PORT_HIGH] = iobase >> 8;

    memory_region_init_io(&s->io, OBJECT(s),
                          &lpc47m157_io_ops, s, "lpc47m157", 2);
    isa_register_ioport(isa, &s->io, iobase);

    /* init serial cores */
    for (i = 0; i < 2; i++) {
        Chardev *chr = serial_hd(i);
        if (chr == NULL) {
            char name[5];
            snprintf(name, sizeof(name), "ser%d", i);
            chr = qemu_chr_new(name, "null", NULL);
        }

        SerialState *ss = &s->serial[i].state;
        ss->baudbase = 115200;
        qdev_prop_set_chr(dev, i == 0 ? "chardev0" : "chardev1", chr);
        serial_realize_core(ss, errp);
        memory_region_init_io(&ss->io, OBJECT(s),
                              &serial_io_ops, ss, "serial", 8);
    }
}

static const VMStateDescription vmstate_lpc47m157 = {
    .name = "lpc47m157",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT(serial[0].state, LPC47M157State, 0,
                       vmstate_serial, SerialState),
        VMSTATE_STRUCT(serial[1].state, LPC47M157State, 0,
                       vmstate_serial, SerialState),
        VMSTATE_END_OF_LIST()
    }
};

static void lpc47m157_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = lpc47m157_realize;
    dc->vmsd = &vmstate_lpc47m157;
    //dc->reset = pc87312_reset;
    dc->props = lpc47m157_properties;
}

static const TypeInfo lpc47m157_type_info = {
    .name          = "lpc47m157",
    .parent        = TYPE_ISA_DEVICE,
    .instance_size = sizeof(LPC47M157State),
    .class_init    = lpc47m157_class_init,
};

static void lpc47m157_register_types(void)
{
    type_register_static(&lpc47m157_type_info);
}

type_init(lpc47m157_register_types)
