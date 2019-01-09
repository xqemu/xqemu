/*
 * QEMU USB XID Devices
 *
 * Copyright (c) 2013 espes
 * Copyright (c) 2017 Jannik Vogel
 * Copyright (c) 2018 Matt Borgerson
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
#include "hw/hw.h"
#include "ui/console.h"
#include "hw/usb.h"
#include "hw/usb/desc.h"
#include "ui/input.h"

#include <SDL2/SDL.h>

//#define FORCE_FEEDBACK
#define UPDATE

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

#define TYPE_USB_XID_SDL "usb-xbox-gamepad-sdl"
#define USB_XID(obj) OBJECT_CHECK(USBXIDState, (obj), TYPE_USB_XID_SDL)

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIALNUMBER,
};

static const USBDescStrings desc_strings = {
    [STR_MANUFACTURER]     = "QEMU",
    [STR_PRODUCT]          = "Microsoft Gamepad",
    [STR_SERIALNUMBER]     = "1",
};

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
    XIDGamepadReport in_state;
    XIDGamepadReport in_state_capabilities;
    XIDGamepadOutputReport out_state;
    XIDGamepadOutputReport out_state_capabilities;

    uint8_t device_index;
    SDL_GameController *sdl_gamepad;
#ifdef FORCE_FEEDBACK
    SDL_Haptic *sdl_haptic;
    int sdl_haptic_effect_id;
#endif
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
            .bmAttributes          = USB_CFG_ATT_ONE,
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
        .iManufacturer     = STR_MANUFACTURER,
        .iProduct          = STR_PRODUCT,
        .iSerialNumber     = STR_SERIALNUMBER,
    },
    .full = &desc_device_xbox_gamepad,
    .str  = desc_strings,
};

static const XIDDesc desc_xid_xbox_gamepad = {
    .bLength = 0x10,
    .bDescriptorType = USB_DT_XID,
    .bcdXid = 0x100,
    .bType = 1,
    .bSubType = 1,
    .bMaxInputReportSize = 20,
    .bMaxOutputReportSize = 6,
    .wAlternateProductIds = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF },
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

static void update_output(USBXIDState *s) {
#ifdef FORCE_FEEDBACK
    if (s->sdl_haptic == NULL) { return; }

    SDL_HapticLeftRight effect = {
        .type = SDL_HAPTIC_LEFTRIGHT,
        .length = SDL_HAPTIC_INFINITY,
        /* FIXME: Might be left/right inverted */
        .large_magnitude = s->out_state.right_actuator_strength,
        .small_magnitude = s->out_state.left_actuator_strength
    };

    if (s->sdl_haptic_effect_id == -1) {
        int effect_id = SDL_HapticNewEffect(s->sdl_haptic,
                                            (SDL_HapticEffect*)&effect);
        if (effect_id == -1) {
            fprintf(stderr, "Failed to upload haptic effect!\n");
            SDL_HapticClose(s->sdl_haptic);
            s->sdl_haptic = NULL;
            return;
        }
        SDL_HapticRunEffect(s->sdl_haptic, effect_id, 1);

        s->sdl_haptic_effect_id = effect_id;

    } else {
        SDL_HapticUpdateEffect(s->sdl_haptic, s->sdl_haptic_effect_id,
                               (SDL_HapticEffect*)&effect);
    }
#endif
}

static void update_input(USBXIDState *s)
{
    int i, state;

#ifdef UPDATE
    SDL_GameControllerUpdate();
#endif

    /* Buttons */
    const int button_map_analog[6][2] = {
        { GAMEPAD_A, SDL_CONTROLLER_BUTTON_A },
        { GAMEPAD_B, SDL_CONTROLLER_BUTTON_B },
        { GAMEPAD_X, SDL_CONTROLLER_BUTTON_X },
        { GAMEPAD_Y, SDL_CONTROLLER_BUTTON_Y },
        { GAMEPAD_BLACK, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
        { GAMEPAD_WHITE, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    };

    const int button_map_binary[8][2] = {
        { GAMEPAD_BACK, SDL_CONTROLLER_BUTTON_BACK },
        { GAMEPAD_START, SDL_CONTROLLER_BUTTON_START },
        { GAMEPAD_LEFT_THUMB, SDL_CONTROLLER_BUTTON_LEFTSTICK },
        { GAMEPAD_RIGHT_THUMB, SDL_CONTROLLER_BUTTON_RIGHTSTICK },
        { GAMEPAD_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_UP },
        { GAMEPAD_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
        { GAMEPAD_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
        { GAMEPAD_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
    };

    for (i = 0; i < 6; i++) {
        state = SDL_GameControllerGetButton(s->sdl_gamepad,
                                            button_map_analog[i][1]);
        s->in_state.bAnalogButtons[button_map_analog[i][0]] = state ? 0xff : 0;
    }

    s->in_state.wButtons = 0;
    for (i = 0; i < 8; i++) {
        state = SDL_GameControllerGetButton(s->sdl_gamepad,
                                            button_map_binary[i][1]);
        if (state) {
            s->in_state.wButtons |= BUTTON_MASK(button_map_binary[i][0]);
        }
    }

    /* Triggers */
    state = SDL_GameControllerGetAxis(s->sdl_gamepad,
                                      SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    s->in_state.bAnalogButtons[GAMEPAD_LEFT_TRIGGER] = state >> 8;

    state = SDL_GameControllerGetAxis(s->sdl_gamepad,
                                      SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    s->in_state.bAnalogButtons[GAMEPAD_RIGHT_TRIGGER] = state >> 8;

    /* Analog sticks */
    s->in_state.sThumbLX = SDL_GameControllerGetAxis(s->sdl_gamepad,
                                SDL_CONTROLLER_AXIS_LEFTX);

    s->in_state.sThumbLY = -SDL_GameControllerGetAxis(s->sdl_gamepad,
                                SDL_CONTROLLER_AXIS_LEFTY) - 1;

    s->in_state.sThumbRX = SDL_GameControllerGetAxis(s->sdl_gamepad,
                                SDL_CONTROLLER_AXIS_RIGHTX);

    s->in_state.sThumbRY = -SDL_GameControllerGetAxis(s->sdl_gamepad,
                                SDL_CONTROLLER_AXIS_RIGHTY) - 1;
}

static void usb_xid_handle_reset(USBDevice *dev)
{
    DPRINTF("xid reset\n");
}

static void usb_xid_handle_control(USBDevice *dev, USBPacket *p,
               int request, int value, int index, int length, uint8_t *data)
{
    USBXIDState *s = (USBXIDState *)dev;

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
        update_input(s);
        if (value == 0x0100) { /* input */
            if (length <= s->in_state.bLength) {
                memcpy(data, &s->in_state, s->in_state.bLength);
                p->actual_length = length;
            } else {
                p->status = USB_RET_STALL;
            }
        } else {
            p->status = USB_RET_STALL;
            assert(false);
        }
        break;
    case ClassInterfaceOutRequest | HID_SET_REPORT:
        DPRINTF("xid SET_REPORT 0x%x\n", value);
        if (value == 0x0200) { /* output */
            /* Read length, then the entire packet */
            if (length == s->out_state.length) {
                memcpy(&s->out_state, data, sizeof(s->out_state));

                /* FIXME: This should also be a STALL */
                assert(s->out_state.length == sizeof(s->out_state));

                p->actual_length = length;
            } else {
                p->status = USB_RET_STALL;
            }
            update_output(s);
        } else {
            p->status = USB_RET_STALL;
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
            p->status = USB_RET_STALL;
            assert(false);
        }
        break;
    case VendorInterfaceRequest | XID_GET_CAPABILITIES:
        DPRINTF("xid XID_GET_CAPABILITIES 0x%x\n", value);
        if (value == 0x0100) {
            if (length > s->in_state_capabilities.bLength) {
                length = s->in_state_capabilities.bLength;
            }
            memcpy(data, &s->in_state_capabilities, length);
            p->actual_length = length;
        } else if (value == 0x0200) {
            if (length > s->out_state_capabilities.length) {
                length = s->out_state_capabilities.length;
            }
            memcpy(data, &s->out_state_capabilities, length);
            p->actual_length = length;
        } else {
            p->status = USB_RET_STALL;
            assert(false);
        }
        break;
    case ((USB_DIR_IN|USB_TYPE_CLASS|USB_RECIP_DEVICE)<<8)
             | USB_REQ_GET_DESCRIPTOR:
        /* FIXME: ! */
        DPRINTF("xid unknown xpad request 0x%x: value = 0x%x\n",
                request, value);
        memset(data, 0x00, length);
        //FIXME: Intended for the hub: usbd_get_hub_descriptor, UT_READ_CLASS?!
        p->status = USB_RET_STALL;
        //assert(false);
        break;
    case ((USB_DIR_OUT|USB_TYPE_STANDARD|USB_RECIP_ENDPOINT)<<8)
             | USB_REQ_CLEAR_FEATURE:
        /* FIXME: ! */
        DPRINTF("xid unknown xpad request 0x%x: value = 0x%x\n",
                request, value);
        memset(data, 0x00, length);
        p->status = USB_RET_STALL;
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
        if (p->ep->nr == 2) {
            usb_packet_copy(p, &s->out_state, s->out_state.length);
            update_output(s);
        } else {
            assert(false);
        }
        break;
    default:
        p->status = USB_RET_STALL;
        assert(false);
        break;
    }
}

#if 0
static void usb_xid_handle_destroy(USBDevice *dev)
{
    USBXIDState *s = DO_UPCAST(USBXIDState, dev, dev);
    DPRINTF("xid handle_destroy\n");
#ifdef FORCE_FEEDBACK
    if (s->sdl_haptic) {
        SDL_HapticClose(s->sdl_haptic);
    }
#endif
    SDL_JoystickClose(s->sdl_gamepad);
}
#endif

static void usb_xbox_gamepad_unrealize(USBDevice *dev, Error **errp)
{
}

static void usb_xid_class_initfn(ObjectClass *klass, void *data)
{
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->handle_reset   = usb_xid_handle_reset;
    uc->handle_control = usb_xid_handle_control;
    uc->handle_data    = usb_xid_handle_data;
    // uc->handle_destroy = usb_xid_handle_destroy;
    uc->handle_attach  = usb_desc_attach;
}

static void usb_xbox_gamepad_realize(USBDevice *dev, Error **errp)
{
    USBXIDState *s = USB_XID(dev);
    usb_desc_create_serial(dev);
    usb_desc_init(dev);
    s->intr = usb_ep_get(dev, USB_TOKEN_IN, 2);

    s->in_state.bLength = sizeof(s->in_state);
    s->in_state.bReportId = 0;

    s->out_state.length = sizeof(s->out_state);
    s->out_state.report_id = 0;

    s->xid_desc = &desc_xid_xbox_gamepad;

    memset(&s->in_state_capabilities, 0xFF, sizeof(s->in_state_capabilities));
    s->in_state_capabilities.bLength = sizeof(s->in_state_capabilities);
    s->in_state_capabilities.bReportId = 0;

    memset(&s->out_state_capabilities, 0xFF, sizeof(s->out_state_capabilities));
    s->out_state_capabilities.length = sizeof(s->out_state_capabilities);
    s->out_state_capabilities.report_id = 0;

    /* FIXME: Make sure SDL was init before */
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "SDL failed to initialize joystick subsystem\n");
        exit(1);
    }

    s->sdl_gamepad = SDL_GameControllerOpen(s->device_index);
    if (s->sdl_gamepad == NULL) {
        /* FIXME: More appropriate qemu error handling */
        fprintf(stderr, "Couldn't open joystick %d\n", s->device_index);
        exit(1);
    }

    const char* name = SDL_GameControllerName(s->sdl_gamepad);
    printf("Found game controller %d (%s)\n", s->device_index, name);

#ifndef UPDATE
    /* We could update the joystick in the usb event handlers, but that would
     * probably pause emulation until data is ready + we'd also hammer SDL with
     * SDL_JoystickUpdate calls if the games are programmed poorly.
     */
    SDL_JoystickEventState(SDL_ENABLE);
#endif

#ifdef FORCE_FEEDBACK
    s->sdl_haptic = SDL_HapticOpenFromJoystick(s->sdl_gamepad);
    if (s->sdl_haptic == NULL) {
        fprintf(stderr, "Joystick doesn't support haptic\n");
    } else {
        if ((SDL_HapticQuery(s->sdl_haptic) & SDL_HAPTIC_LEFTRIGHT) == 0) {
            fprintf(stderr, "Joystick doesn't support necessary haptic effect\n");
            SDL_HapticClose(s->sdl_haptic);
            s->sdl_haptic = NULL;
        }
    }
#endif
}

static Property xid_sdl_properties[] = {
    DEFINE_PROP_UINT8("index", USBXIDState, device_index, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static const VMStateDescription vmstate_usb_xbox = {
    .name = TYPE_USB_XID_SDL,
    .unmigratable = 1,
};

static void usb_xbox_gamepad_class_initfn(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc   = "Microsoft Xbox Controller";
    uc->usb_desc       = &desc_xbox_gamepad;
    uc->realize        = usb_xbox_gamepad_realize;
    uc->unrealize      = usb_xbox_gamepad_unrealize;
    usb_xid_class_initfn(klass, data);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
    dc->vmsd  = &vmstate_usb_xbox;
    dc->props = xid_sdl_properties;
    dc->desc  = "Microsoft Xbox Controller";
}

static const TypeInfo usb_xbox_gamepad_info = {
    .name          = TYPE_USB_XID_SDL,
    .parent        = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBXIDState),
    .class_init    = usb_xbox_gamepad_class_initfn,
};

static void usb_xid_register_types(void)
{
    type_register_static(&usb_xbox_gamepad_info);
}

type_init(usb_xid_register_types)
