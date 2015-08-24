/*
 * QEMU USB XID Devices
 *
 * Copyright (c) 2013 espes
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "hw/hw.h"
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"

#include <SDL/SDL.h>

#define UPDATE 1

//#define DEBUG_XID
#ifdef DEBUG_XID
#define DPRINTF printf
#else
#define DPRINTF(...)
#endif

/*
 * http://xbox-linux.cvs.sourceforge.net/viewvc/xbox-linux/kernel-2.6/drivers/usb/input/xpad.c
 * http://euc.jp/periphs/xbox-controller.en.html
 * http://euc.jp/periphs/xbox-pad-desc.txt
 */

#define USB_CLASS_XID  0x58
#define USB_DT_XID     0x42


#define HID_GET_REPORT       0x01
#define HID_SET_REPORT       0x09
#define XID_GET_CAPABILITIES 0x01



typedef struct XIDDesc {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdXid;
    uint8_t bType;
    uint8_t bSubType;
    uint8_t bMaxInputReportSize;
    uint8_t bMaxOutputReportSize;
    uint16_t wAlternateProductIds[4];
} QEMU_PACKED XIDDesc;

typedef struct XIDGamepadReport {
    uint8_t bReportId;
    uint8_t bLength;
    uint16_t wButtons;
    uint8_t bAnalogButtons[8];
    int16_t sThumbLX;
    int16_t sThumbLY;
    int16_t sThumbRX;
    int16_t sThumbRY;
} QEMU_PACKED XIDGamepadReport;

typedef struct XIDGamepadOutputReport {
    uint8_t report_id; //FIXME: is this correct?
    uint8_t length;
    uint16_t left_actuator_strength;
    uint16_t right_actuator_strength;
} QEMU_PACKED XIDGamepadOutputReport;


typedef struct USBXIDState {
    USBDevice dev;
    USBEndpoint *intr;

    const XIDDesc *xid_desc;

    char* device;
    SDL_Joystick* sdl_joystick;
    XIDGamepadReport in_state;
    XIDGamepadOutputReport out_state;
} USBXIDState;

static const USBDescIface desc_iface_xbox_gamepad = {
    .bInterfaceNumber              = 0,
    .bNumEndpoints                 = 2,
    .bInterfaceClass               = USB_CLASS_XID,
    .bInterfaceSubClass            = 0x42,
    .bInterfaceProtocol            = 0x00,
    .eps = (USBDescEndpoint[]) {
        {
            .bEndpointAddress      = USB_DIR_IN | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 0x20,
            .bInterval             = 4,
        },
        {
            .bEndpointAddress      = USB_DIR_OUT | 0x02,
            .bmAttributes          = USB_ENDPOINT_XFER_INT,
            .wMaxPacketSize        = 0x20,
            .bInterval             = 4,
        },
    },
};

static const USBDescDevice desc_device_xbox_gamepad = {
    .bcdUSB                        = 0x0110,
    .bMaxPacketSize0               = 0x40,
    .bNumConfigurations            = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces        = 1,
            .bConfigurationValue   = 1,
            .bmAttributes          = 0x80,
            .bMaxPower             = 50,
            .nif = 1,
            .ifs = &desc_iface_xbox_gamepad,
        },
    },
};

static const USBDesc desc_xbox_gamepad = {
    .id = {
        .idVendor          = 0x045e,
        .idProduct         = 0x0202,
        .bcdDevice         = 0x0100,
    },
    .full = &desc_device_xbox_gamepad,
};

static const XIDDesc desc_xid_xbox_gamepad = {
    .bLength = 0x10,
    .bDescriptorType = USB_DT_XID,
    .bcdXid = 1,
    .bType = 1,
    .bSubType = 1,
    .bMaxInputReportSize = 0x20,
    .bMaxOutputReportSize = 0x6,
    .wAlternateProductIds = {-1, -1, -1, -1},
};


#define GAMEPAD_A                0
#define GAMEPAD_B                1
#define GAMEPAD_X                2
#define GAMEPAD_Y                3
#define GAMEPAD_BLACK            4
#define GAMEPAD_WHITE            5
#define GAMEPAD_LEFT_TRIGGER     6
#define GAMEPAD_RIGHT_TRIGGER    7

#define GAMEPAD_DPAD_UP          8
#define GAMEPAD_DPAD_DOWN        9
#define GAMEPAD_DPAD_LEFT        10
#define GAMEPAD_DPAD_RIGHT       11
#define GAMEPAD_START            12
#define GAMEPAD_BACK             13
#define GAMEPAD_LEFT_THUMB       14
#define GAMEPAD_RIGHT_THUMB      15

#define BUTTON_MASK(button) (1 << ((button) - GAMEPAD_DPAD_UP))

static void xbox_gamepad_keyboard_event(void *opaque, int keycode)
{
    USBXIDState *s = opaque;

#if 0
    bool up = keycode & 0x80;
    QKeyCode code = index_from_keycode(keycode & 0x7f);
    if (code >= Q_KEY_CODE_MAX) return;

    int button = gamepad_mapping[code];

    DPRINTF("xid keyboard_event %x - %d %d %d\n", keycode, code, button, up);

    uint16_t mask;
    switch (button) {
    case GAMEPAD_A ... GAMEPAD_RIGHT_TRIGGER:
        s->in_state.bAnalogButtons[button] = up?0:0xff;
        break;
    case GAMEPAD_DPAD_UP ... GAMEPAD_RIGHT_THUMB:
        mask = (1 << (button-GAMEPAD_DPAD_UP));
        s->in_state.wButtons &= ~mask;
        if (!up) s->in_state.wButtons |= mask;
        break;
    default:
        break;
    }

#endif

}

static void update_input(USBXIDState *s)
{

    /* Clear input */
    s->in_state.wButtons = 0;

#ifdef UPDATE
    SDL_JoystickUpdate();
#endif

    /* Buttons */
    /* FIXME: Add some ramping options to emulate analog buttons? */
    s->in_state.bAnalogButtons[GAMEPAD_A] = SDL_JoystickGetButton(
                                                s->sdl_joystick, 0) ?
                                                    0xFF : 0x00;
    s->in_state.bAnalogButtons[GAMEPAD_B] = SDL_JoystickGetButton(
                                                s->sdl_joystick, 1) ?
                                                    0xFF : 0x00;
    s->in_state.bAnalogButtons[GAMEPAD_X] = SDL_JoystickGetButton(
                                                s->sdl_joystick, 2) ?
                                                    0xFF : 0x00;
    s->in_state.bAnalogButtons[GAMEPAD_Y] = SDL_JoystickGetButton(
                                                s->sdl_joystick, 3) ?
                                                    0xFF : 0x00;
    s->in_state.bAnalogButtons[GAMEPAD_BLACK] = SDL_JoystickGetButton(
                                                    s->sdl_joystick, 4) ?
                                                        0xFF : 0x00;
    s->in_state.bAnalogButtons[GAMEPAD_WHITE] = SDL_JoystickGetButton(
                                                    s->sdl_joystick, 5) ?
                                                        0xFF : 0x00;

    if (SDL_JoystickGetButton(s->sdl_joystick, 6)) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_BACK);
    }
    if (SDL_JoystickGetButton(s->sdl_joystick, 7)) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_START);
    }

    if (SDL_JoystickGetButton(s->sdl_joystick, 9)) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_LEFT_THUMB);
    }
    if (SDL_JoystickGetButton(s->sdl_joystick, 10)) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_RIGHT_THUMB);
    }

    /* Triggers */
    s->in_state.bAnalogButtons[GAMEPAD_LEFT_TRIGGER] = SDL_JoystickGetAxis(
                                             s->sdl_joystick, 2) / 0x100 + 0x80;

    s->in_state.bAnalogButtons[GAMEPAD_RIGHT_TRIGGER] = SDL_JoystickGetAxis(
                                             s->sdl_joystick, 5) / 0x100 + 0x80;

    /* Analog sticks */
    s->in_state.sThumbLX = SDL_JoystickGetAxis(s->sdl_joystick, 0);
    s->in_state.sThumbLY = -SDL_JoystickGetAxis(s->sdl_joystick, 1) - 1;
    s->in_state.sThumbRX = SDL_JoystickGetAxis(s->sdl_joystick, 3);
    s->in_state.sThumbRY = -SDL_JoystickGetAxis(s->sdl_joystick, 4) - 1;

    /* Digital-Pad */
    if (SDL_JoystickGetHat(s->sdl_joystick, 0) & SDL_HAT_UP) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_DPAD_UP);
    }
    if (SDL_JoystickGetHat(s->sdl_joystick, 0) & SDL_HAT_DOWN) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_DPAD_DOWN);
    }
    if (SDL_JoystickGetHat(s->sdl_joystick, 0) & SDL_HAT_LEFT) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_DPAD_LEFT);
    }
    if (SDL_JoystickGetHat(s->sdl_joystick, 0) & SDL_HAT_RIGHT) {
        s->in_state.wButtons |= BUTTON_MASK(GAMEPAD_DPAD_RIGHT);
    }

}

static void usb_xid_handle_reset(USBDevice *dev)
{
    DPRINTF("xid reset\n");
}

static void usb_xid_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);

    DPRINTF("xid handle_control 0x%x 0x%x\n", request, value);

    int ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret >= 0) {
        DPRINTF("xid handled by usb_desc_handle_control: %d\n", ret);
        return;
    }

    switch (request) {
    /* HID requests */
    case ClassInterfaceRequest | HID_GET_REPORT:
        DPRINTF("xid GET_REPORT 0x%x\n", value);
        if (value == 0x100) { /* input */
            update_input(s);
            assert(s->in_state.bLength <= length);
//          s->in_state.bReportId++; /* FIXME: I'm not sure if bReportId is just a counter */
            memcpy(data, &s->in_state, s->in_state.bLength);
            p->actual_length = s->in_state.bLength;
        } else {
            assert(false);
        }
        break;
    case ClassInterfaceOutRequest | HID_SET_REPORT:
        DPRINTF("xid SET_REPORT 0x%x\n", value);
        if (value == 0x200) { /* output */
            /* Read length, then the entire packet */
            memcpy(&s->out_state, data, sizeof(s->out_state));
            assert(s->out_state.length == sizeof(s->out_state));
            assert(s->out_state.length <= length);
            //FIXME: Check actuator endianess
            DPRINTF("Set rumble power to 0x%x, 0x%x\n",
                    s->out_state.left_actuator_strength,
                    s->out_state.right_actuator_strength);
            p->actual_length = s->out_state.length;
        } else {
            assert(false);
        }
        break;
    /* XID requests */
    case VendorInterfaceRequest | USB_REQ_GET_DESCRIPTOR:
        DPRINTF("xid GET_DESCRIPTOR 0x%x\n", value);
        if (value == 0x4200) {
            assert(s->xid_desc->bLength <= length);
            memcpy(data, s->xid_desc, s->xid_desc->bLength);
            p->actual_length = s->xid_desc->bLength;
        } else {
            assert(false);
        }
        break;
    case VendorInterfaceRequest | XID_GET_CAPABILITIES:
        DPRINTF("xid XID_GET_CAPABILITIES 0x%x\n", value);
        //FIXME: !
        p->status = USB_RET_STALL;
        //assert(false);
        break;
    case ((USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_DEVICE)<<8) | 0x06:
        DPRINTF("xid unknown xpad request 1: value = 0x%x\n", value);
        memset(data, 0x00, length);
        //FIXME: Intended for the hub: usbd_get_hub_descriptor, UT_READ_CLASS?!
        p->status = USB_RET_STALL;
        //assert(false);
        break;
    default:
        DPRINTF("xid USB stalled on request 0x%x value 0x%x\n", request, value);
        p->status = USB_RET_STALL;
        assert(false);
        break;
    }
}

static void usb_xid_handle_data(USBDevice *dev, USBPacket *p)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);

    DPRINTF("xid handle_data 0x%x %d 0x%zx\n", p->pid, p->ep->nr, p->iov.size);

    switch (p->pid) {
    case USB_TOKEN_IN:
        if (p->ep->nr == 2) {
            update_input(s);
            usb_packet_copy(p, &s->in_state, s->in_state.bLength);
        } else {
            assert(false);
        }
        break;
    case USB_TOKEN_OUT:
        p->status = USB_RET_STALL;
        break;
    default:
        p->status = USB_RET_STALL;
        assert(false);
        break;
    }
}

static void usb_xid_handle_destroy(USBDevice *dev)
{
    DPRINTF("xid handle_destroy\n");
}

static void usb_xid_class_initfn(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->handle_reset   = usb_xid_handle_reset;
    uc->handle_control = usb_xid_handle_control;
    uc->handle_data    = usb_xid_handle_data;
    uc->handle_destroy = usb_xid_handle_destroy;
    uc->handle_attach  = usb_desc_attach;
}

static int usb_xbox_gamepad_initfn(USBDevice *dev)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 2);

    s->in_state.bLength = sizeof(s->in_state);
    s->out_state.length = sizeof(s->out_state);

    const char* search_name = s->device;

    /* FIXME: Make sure SDL was init before */
    if (SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
        fprintf(stderr, "SDL failed to initialize joystick subsystem\n");
        exit(1);
    }

    int num_joysticks = SDL_NumJoysticks();

    printf("Found %d joystick devices\n", num_joysticks);

    int i;
    for(i = 0; i < num_joysticks; i++) {
        const char* name = SDL_JoystickName(i);
        printf("Found '%s'\n", name);
        if (!strcmp(name, search_name)) {
            break;
        }
    }

    if (search_name == NULL) {
        fprintf(stderr, "No device name specified for xid-sdl\n");
        exit(1);
    }

    if (i == num_joysticks) {
        /* FIXME: More appropiate qemu error handling */
        fprintf(stderr, "Couldn't find joystick '%s'\n", search_name);
        exit(1);
    }
    SDL_Joystick* sdl_joystick = SDL_JoystickOpen(i);
    if (sdl_joystick == NULL) {
        fprintf(stderr, "Couldn't open joystick '%s' (Index %d)\n", search_name, i);
        exit(1);
    }

#ifndef UPDATE
    /* We could update the joystick in the usb event handlers, but that would
     * probably pause emulation until data is ready + we'd also hammer SDL with
     * SDL_JoystickUpdate calls if the games are programmed poorly.
     */
    SDL_JoystickEventState(SDL_ENABLE);
#endif
    s->sdl_joystick = sdl_joystick;


/* Used to find mappings. Should probably end up in some sort of gui */
#if 0
    int hats = SDL_JoystickNumHats(sdl_joystick);
    int axes = SDL_JoystickNumAxes(sdl_joystick);
    int buttons = SDL_JoystickNumButtons(sdl_joystick);
    int balls = SDL_JoystickNumBalls(sdl_joystick);
    while(1) {
#ifdef UPDATE
        SDL_JoystickUpdate();
#endif
        for(i = 0; i < hats; i++) {
            printf("Hat %d: %d\n", i, (Uint8)SDL_JoystickGetHat(sdl_joystick, i));
        }
        for(i = 0; i < axes; i++) {
            printf("Axis %d: %d\n", i, (Sint16)SDL_JoystickGetAxis(sdl_joystick, i));
        }
        for(i = 0; i < buttons; i++) {
            printf("Button %d: %d\n", i, (Uint8)SDL_JoystickGetButton(sdl_joystick, i));
        }
        for(i = 0; i < balls; i++) {
            int dx, dy;
            int ret = SDL_JoystickGetBall(sdl_joystick, i, &dx, &dy);
            printf("Ball %d: ret=%d, dx=%d, dy=%d\n", i, ret, dx, dy);
        }
        usleep(100*1000);
    }
#endif


    s->xid_desc = &desc_xid_xbox_gamepad;

    return 0;
}

static Property xid_sdl_properties[] = {
    DEFINE_PROP_STRING("device", USBXIDState, device),
    DEFINE_PROP_END_OF_LIST(),
};

static void usb_xbox_gamepad_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    usb_xid_class_initfn(klass, data);
    uc->init           = usb_xbox_gamepad_initfn;
    uc->product_desc   = "Microsoft Xbox Controller";
    uc->usb_desc       = &desc_xbox_gamepad;
    //dc->vmsd = &vmstate_usb_kbd;
    dc->props = xid_sdl_properties;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo usb_xbox_gamepad_info = {
    .name          = "usb-xbox-gamepad-sdl",
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBXIDState),
    .class_init    = usb_xbox_gamepad_class_initfn,
};

static void usb_xid_register_types(void)
{
    type_register_static(&usb_xbox_gamepad_info);
}

type_init(usb_xid_register_types)
