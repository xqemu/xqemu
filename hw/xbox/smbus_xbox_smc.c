/*
 * QEMU SMBus Xbox System Management Controller
 *
 * Copyright (c) 2011 espes
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/option.h"
#include "hw/hw.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/smbus.h"
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "smbus.h"

#define TYPE_XBOX_SMC "smbus-xbox-smc"
#define XBOX_SMC(obj) OBJECT_CHECK(SMBusSMCDevice, (obj), TYPE_XBOX_SMC)

// #define DEBUG
#ifdef DEBUG
# define DPRINTF(format, ...) printf(format, ## __VA_ARGS__)
#else
# define DPRINTF(format, ...) do { } while (0)
#endif

/*
 * Hardware is a PIC16LC
 * http://www.xbox-linux.org/wiki/PIC
 */

#define SMC_REG_VER                 0x01
#define SMC_REG_POWER               0x02
#define     SMC_REG_POWER_RESET         0x01
#define     SMC_REG_POWER_CYCLE         0x40
#define     SMC_REG_POWER_SHUTDOWN      0x80
#define SMC_REG_TRAYSTATE           0x03
#define SMC_REG_AVPACK              0x04
#define     SMC_REG_AVPACK_SCART        0x00
#define     SMC_REG_AVPACK_HDTV         0x01
#define     SMC_REG_AVPACK_VGA_SOG      0x02
#define     SMC_REG_AVPACK_SVIDEO       0x04
#define     SMC_REG_AVPACK_COMPOSITE    0x06
#define     SMC_REG_AVPACK_VGA          0x07
#define SMC_REG_FANMODE             0x05
#define SMC_REG_FANSPEED            0x06
#define SMC_REG_LEDMODE             0x07
#define SMC_REG_LEDSEQ              0x08
#define SMC_REG_CPUTEMP             0x09
#define SMC_REG_BOARDTEMP           0x0a
#define SMC_REG_TRAYEJECT           0x0c
#define SMC_REG_INTACK              0x0d
#define SMC_REG_INTSTATUS           0x11
#define     SMC_REG_INTSTATUS_POWER         0x01
#define     SMC_REG_INTSTATUS_TRAYCLOSED    0x02
#define     SMC_REG_INTSTATUS_TRAYOPENING   0x04
#define     SMC_REG_INTSTATUS_AVPACK_PLUG   0x08
#define     SMC_REG_INTSTATUS_AVPACK_UNPLUG 0x10
#define     SMC_REG_INTSTATUS_EJECT_BUTTON  0x20
#define     SMC_REG_INTSTATUS_TRAYCLOSING   0x40
#define SMC_REG_RESETONEJECT        0x19
#define SMC_REG_INTEN               0x1a
#define SMC_REG_SCRATCH             0x1b
#define     SMC_REG_SCRATCH_SHORT_ANIMATION 0x04

static const char *smc_version_string = "P01";

typedef struct SMBusSMCDevice {
    SMBusDevice smbusdev;
    int version_string_index;
    uint8_t scratch_reg;
} SMBusSMCDevice;

static void smc_quick_cmd(SMBusDevice *dev, uint8_t read)
{
    DPRINTF("smc_quick_cmd: addr=0x%02x read=%d\n", dev->i2c.address, read);
}

static void smc_send_byte(SMBusDevice *dev, uint8_t val)
{
    DPRINTF("smc_send_byte: addr=0x%02x val=0x%02x\n", dev->i2c.address, val);
}

static uint8_t smc_receive_byte(SMBusDevice *dev)
{
    DPRINTF("smc_receive_byte: addr=0x%02x\n", dev->i2c.address);
    return 0;
}

static void smc_write_data(SMBusDevice *dev, uint8_t cmd, uint8_t *buf, int len)
{
    SMBusSMCDevice *smc = (SMBusSMCDevice *) dev;
    DPRINTF("smc_write_byte: addr=0x%02x cmd=0x%02x val=0x%02x\n",
           dev->i2c.address, cmd, buf[0]);

    switch (cmd) {
    case SMC_REG_VER:
        /* version string reset */
        smc->version_string_index = buf[0];
        break;

    case SMC_REG_POWER:
        if (buf[0] & (SMC_REG_POWER_RESET | SMC_REG_POWER_CYCLE)) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        } else if (buf[0] & SMC_REG_POWER_SHUTDOWN) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        break;

    case SMC_REG_SCRATCH:
        smc->scratch_reg = buf[0];
        break;

    /* challenge response
     * (http://www.xbox-linux.org/wiki/PIC_Challenge_Handshake_Sequence) */
    case 0x20:
        break;
    case 0x21:
        break;

    default:
        break;
    }
}

static uint8_t smc_read_data(SMBusDevice *dev, uint8_t cmd, int n)
{
    SMBusSMCDevice *smc = (SMBusSMCDevice *)dev;
    DPRINTF("smc_read_data: addr=0x%02x cmd=0x%02x n=%d\n",
            dev->i2c.address, cmd, n);

    switch (cmd) {
    case SMC_REG_VER:
        return smc_version_string[
            smc->version_string_index++ % (sizeof(smc_version_string) - 1)];

    case SMC_REG_AVPACK:
        /* pretend to have a composite av pack plugged in */
        return SMC_REG_AVPACK_COMPOSITE;

    case SMC_REG_SCRATCH:
        return smc->scratch_reg;

    /* challenge request:
     * must be non-0 */
    case 0x1c:
        return 0x52;
    case 0x1d:
        return 0x72;
    case 0x1e:
        return 0xea;
    case 0x1f:
        return 0x46;

    default:
        break;
    }

    return 0;
}

static void smbus_smc_realize(DeviceState *dev, Error **errp)
{
    SMBusSMCDevice *smc = XBOX_SMC(dev);

    smc->version_string_index = 0;
    smc->scratch_reg = 0;

    if (object_property_get_bool(qdev_get_machine(), "short-animation", NULL)) {
        smc->scratch_reg = SMC_REG_SCRATCH_SHORT_ANIMATION;
    }
}

static void smbus_smc_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SMBusDeviceClass *sc = SMBUS_DEVICE_CLASS(klass);

    dc->realize = smbus_smc_realize;
    sc->quick_cmd = smc_quick_cmd;
    sc->send_byte = smc_send_byte;
    sc->receive_byte = smc_receive_byte;
    sc->write_data = smc_write_data;
    sc->read_data = smc_read_data;
}

static TypeInfo smbus_smc_info = {
    .name = TYPE_XBOX_SMC,
    .parent = TYPE_SMBUS_DEVICE,
    .instance_size = sizeof(SMBusSMCDevice),
    .class_init = smbus_smc_class_initfn,
};

static void smbus_smc_register_devices(void)
{
    type_register_static(&smbus_smc_info);
}

type_init(smbus_smc_register_devices)

void smbus_xbox_smc_init(I2CBus *smbus, int address)
{
    DeviceState *smc;
    smc = qdev_create((BusState *)smbus, "smbus-xbox-smc");
    qdev_prop_set_uint8(smc, "address", address);
    qdev_init_nofail(smc);
}
