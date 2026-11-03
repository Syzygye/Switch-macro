#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_usb.h>
#include <usb.h>
#include <usb_hid.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define EP_GAMEPAD_IN 0x81
#define EP_GAMEPAD_OUT 0x02
#define EP_KEYBOARD_IN 0x83
#define EP_KEYBOARD_OUT 0x03
#define HID_PACKET_SIZE 8

#define SWITCH_HAT_NONE 0x08
#define SWITCH_BUTTON_Y (1U << 0)
#define SWITCH_BUTTON_B (1U << 1)
#define SWITCH_BUTTON_A (1U << 2)
#define SWITCH_BUTTON_X (1U << 3)
#define SWITCH_BUTTON_L (1U << 4)
#define SWITCH_BUTTON_R (1U << 5)
#define SWITCH_BUTTON_ZL (1U << 6)
#define SWITCH_BUTTON_ZR (1U << 7)
#define SWITCH_BUTTON_MINUS (1U << 8)
#define SWITCH_BUTTON_PLUS (1U << 9)
#define SWITCH_BUTTON_L3 (1U << 10)
#define SWITCH_BUTTON_R3 (1U << 11)
#define SWITCH_BUTTON_HOME (1U << 12)
#define SWITCH_BUTTON_CAPTURE (1U << 13)

#define MACRO_DIRECTORY "/ext/apps_data/switch_macro"
#define APP_DATA_DIRECTORY "/ext/apps_data"
#define MACRO_DEFAULT_NAME "macros.txt"
#define MACRO_FILENAME_MAX 32U
#define MACRO_BASENAME_MAX 27U
#define MACRO_MAX_FILES 24U
#define MACRO_LIST_VISIBLE 4
#define MACRO_FILE_MAX_BYTES 24576U
#define MACRO_MAX_STEPS 512U
#define MACRO_MAX_STEP_MS 60000U
#define LIVE_QUEUE_DEPTH 1U
#define LIVE_FRAME_MAGIC 0xC7U
#define LIVE_FRAME_SIZE 9U
#define SIMULATOR_NO_CONTROL 0xFFU
#define SIMULATOR_STICK_STEP 48U
#define LIVE_RECORD_HEADER "# Recorded on Flipper Zero\n# STATE controls duration_ms\n"

typedef struct __attribute__((packed)) {
    uint16_t buttons;
    uint8_t hat;
    uint8_t lx, ly, rx, ry;
    uint8_t vendor;
} SwitchReport;

typedef struct __attribute__((packed)) {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycode[6];
} KeyboardReport;

typedef enum {
    RunReady,
    RunSending,
    RunLive,
    RunFinished,
    RunCanceled,
    RunError,
} RunState;

typedef enum {
    MacroUiHome,
    MacroUiList,
    MacroUiDetails,
    MacroUiActions,
    MacroUiRename,
    MacroUiDeleteConfirm,
    MacroUiSimulator,
} MacroUiScreen;

typedef enum {
    LiveUiMenu,
    LiveUiRecorder,
    LiveUiController,
} LiveUiScreen;

typedef enum {
    RenameKeyCharacter,
    RenameKeyBackspace,
    RenameKeySymbols,
    RenameKeySpace,
    RenameKeySave,
} RenameKeyAction;

typedef struct {
    char value;
    uint8_t x;
    uint8_t width;
    RenameKeyAction action;
} RenameKey;

typedef struct {
    char name[MACRO_FILENAME_MAX];
} MacroFile;

typedef enum {
    SimulatorControlL,
    SimulatorControlZL,
    SimulatorControlHome,
    SimulatorControlZR,
    SimulatorControlR,
    SimulatorControlLeftStick,
    SimulatorControlL3,
    SimulatorControlDpadUp,
    SimulatorControlDpadLeft,
    SimulatorControlDpadDown,
    SimulatorControlDpadRight,
    SimulatorControlMinus,
    SimulatorControlCapture,
    SimulatorControlPlus,
    SimulatorControlRightStick,
    SimulatorControlR3,
    SimulatorControlX,
    SimulatorControlY,
    SimulatorControlA,
    SimulatorControlB,
    SimulatorControlCount,
} SimulatorControlId;

typedef struct {
    const char* name;
    uint16_t button;
    uint8_t hat;
    uint8_t x;
    uint8_t y;
} SimulatorControl;

static const SimulatorControl simulator_controls[SimulatorControlCount] = {
    [SimulatorControlL] = {"L", SWITCH_BUTTON_L, SWITCH_HAT_NONE, 26U, 9U},
    [SimulatorControlZL] = {"ZL", SWITCH_BUTTON_ZL, SWITCH_HAT_NONE, 45U, 9U},
    [SimulatorControlHome] = {"HOME", SWITCH_BUTTON_HOME, SWITCH_HAT_NONE, 64U, 15U},
    [SimulatorControlZR] = {"ZR", SWITCH_BUTTON_ZR, SWITCH_HAT_NONE, 83U, 9U},
    [SimulatorControlR] = {"R", SWITCH_BUTTON_R, SWITCH_HAT_NONE, 101U, 9U},
    [SimulatorControlLeftStick] = {"L STICK", 0U, SWITCH_HAT_NONE, 27U, 23U},
    [SimulatorControlL3] = {"L3", SWITCH_BUTTON_L3, SWITCH_HAT_NONE, 41U, 23U},
    [SimulatorControlDpadUp] = {"UP", 0U, 0U, 43U, 30U},
    [SimulatorControlDpadLeft] = {"LEFT", 0U, 6U, 35U, 37U},
    [SimulatorControlDpadDown] = {"DOWN", 0U, 4U, 43U, 45U},
    [SimulatorControlDpadRight] = {"RIGHT", 0U, 2U, 51U, 37U},
    [SimulatorControlMinus] = {"MINUS", SWITCH_BUTTON_MINUS, SWITCH_HAT_NONE, 57U, 23U},
    [SimulatorControlCapture] = {"CAPTURE", SWITCH_BUTTON_CAPTURE, SWITCH_HAT_NONE, 64U, 30U},
    [SimulatorControlPlus] = {"PLUS", SWITCH_BUTTON_PLUS, SWITCH_HAT_NONE, 72U, 23U},
    [SimulatorControlRightStick] = {"R STICK", 0U, SWITCH_HAT_NONE, 82U, 37U},
    [SimulatorControlR3] = {"R3", SWITCH_BUTTON_R3, SWITCH_HAT_NONE, 95U, 48U},
    [SimulatorControlX] = {"X", SWITCH_BUTTON_X, SWITCH_HAT_NONE, 109U, 22U},
    [SimulatorControlY] = {"Y", SWITCH_BUTTON_Y, SWITCH_HAT_NONE, 99U, 33U},
    [SimulatorControlA] = {"A", SWITCH_BUTTON_A, SWITCH_HAT_NONE, 119U, 33U},
    [SimulatorControlB] = {"B", SWITCH_BUTTON_B, SWITCH_HAT_NONE, 109U, 44U},
};

typedef enum {
    MacroLoadOk,
    MacroLoadMissing,
    MacroLoadTooLarge,
    MacroLoadEmpty,
    MacroLoadBadSyntax,
    MacroLoadTooManySteps,
    MacroLoadOutOfMemory,
} MacroLoadError;

typedef enum {
    TxFailureNone,
    TxFailureDisconnected,
    TxFailureBusyTimeout,
    TxFailureEndpointConfig,
    TxFailureWriteRejected,
    TxFailureAckTimeout,
} TxFailure;

typedef struct {
    uint16_t buttons;
    uint8_t hat;
    uint32_t duration_ms;
    uint8_t lx, ly, rx, ry;
    bool state_mode;
} MacroStep;

typedef struct {
    FuriMessageQueue* input_queue;
    ViewPort* view_port;
    Gui* gui;
    FuriThread* worker;
    FuriThread* live_worker;
    FuriHalUsbInterface* previous_usb;
    FuriSemaphore* gamepad_tx_sem;
    FuriSemaphore* keyboard_tx_sem;
    FuriMutex* live_record_mutex;
    FuriMessageQueue* live_queue;
    Bt* bt;
    FuriHalBleProfileBase* ble_profile;
    usbd_device* usb_dev;
    volatile bool connected;
    volatile bool configured;
    volatile bool controller_wake_pending;
    volatile bool stop_requested;
    volatile bool live_stop_requested;
    volatile bool live_active;
    volatile bool live_start_failed;
    volatile LiveUiScreen live_ui_screen;
    LiveUiScreen live_controller_return_screen;
    uint8_t live_recorder_selection;
    volatile BtStatus bt_status;
    volatile RunState state;
    volatile uint16_t completed_steps;
    volatile uint32_t macro_elapsed_ms;
    volatile uint32_t macro_cycles;
    uint32_t macro_total_ms;
    volatile bool macro_loop;
    volatile uint8_t failed_endpoint;
    volatile TxFailure tx_failure;
    volatile int32_t usb_result;
    MacroStep* steps;
    uint16_t step_count;
    MacroLoadError load_error;
    uint16_t load_error_line;
    uint8_t live_rx_frame[LIVE_FRAME_SIZE];
    uint8_t live_rx_count;
    volatile uint16_t live_frames_received;
    volatile uint16_t live_reports_sent;
    volatile uint16_t live_frames_dropped;
    char* live_record_text;
    uint16_t live_record_bytes;
    volatile uint16_t live_record_steps;
    volatile bool live_recording;
    volatile bool live_record_full;
    volatile bool live_record_saved;
    char live_record_saved_name[MACRO_FILENAME_MAX];
    SwitchReport live_record_state;
    SwitchReport live_latest_state;
    bool live_record_has_state;
    bool live_latest_valid;
    uint32_t live_record_last_tick;
    uint32_t live_record_started_tick;
    volatile uint32_t live_record_elapsed_ms;
    MacroFile macro_files[MACRO_MAX_FILES];
    uint8_t macro_count;
    uint8_t macro_index;
    uint8_t macro_list_offset;
    uint8_t macro_action_index;
    uint8_t macro_detail_selection;
    uint8_t home_index;
    uint8_t simulator_selection;
    uint8_t simulator_held_control;
    SwitchReport simulator_report;
    bool simulator_error;
    bool simulator_stick_active;
    MacroUiScreen macro_ui_screen;
    char rename_name[MACRO_BASENAME_MAX + 1U];
    uint8_t rename_position;
    uint8_t rename_keyboard_row;
    uint8_t rename_keyboard_column;
    bool rename_symbols;
    bool rename_cursor_selected;
    char ui_message[32];
} CivSwitchApp;

static const RenameKey rename_alpha_row_0[] = {
    {'Q', 1, 9, RenameKeyCharacter}, {'W', 10, 9, RenameKeyCharacter},
    {'E', 19, 9, RenameKeyCharacter}, {'R', 28, 9, RenameKeyCharacter},
    {'T', 37, 9, RenameKeyCharacter}, {'Y', 46, 9, RenameKeyCharacter},
    {'U', 55, 9, RenameKeyCharacter}, {'I', 64, 9, RenameKeyCharacter},
    {'O', 73, 9, RenameKeyCharacter}, {'P', 82, 9, RenameKeyCharacter},
    {'0', 92, 9, RenameKeyCharacter}, {'1', 102, 9, RenameKeyCharacter},
    {'2', 111, 9, RenameKeyCharacter}, {'3', 120, 9, RenameKeyCharacter},
};

static const RenameKey rename_alpha_row_1[] = {
    {'A', 1, 9, RenameKeyCharacter}, {'S', 10, 9, RenameKeyCharacter},
    {'D', 19, 9, RenameKeyCharacter}, {'F', 28, 9, RenameKeyCharacter},
    {'G', 37, 9, RenameKeyCharacter}, {'H', 46, 9, RenameKeyCharacter},
    {'J', 55, 9, RenameKeyCharacter}, {'K', 64, 9, RenameKeyCharacter},
    {'L', 73, 9, RenameKeyCharacter}, {0, 82, 18, RenameKeyBackspace},
    {'4', 102, 9, RenameKeyCharacter}, {'5', 111, 9, RenameKeyCharacter},
    {'6', 120, 9, RenameKeyCharacter},
};

static const RenameKey rename_alpha_row_2[] = {
    {0, 0, 11, RenameKeySymbols}, {'Z', 13, 9, RenameKeyCharacter},
    {'X', 21, 9, RenameKeyCharacter}, {'C', 29, 9, RenameKeyCharacter},
    {'V', 37, 9, RenameKeyCharacter}, {'B', 45, 9, RenameKeyCharacter},
    {'N', 53, 9, RenameKeyCharacter}, {'M', 61, 9, RenameKeyCharacter},
    {'_', 69, 9, RenameKeyCharacter}, {'-', 77, 9, RenameKeyCharacter},
    {0, 84, 17, RenameKeySave}, {'7', 102, 9, RenameKeyCharacter},
    {'8', 111, 9, RenameKeyCharacter}, {'9', 120, 9, RenameKeyCharacter},
};

static const RenameKey rename_symbol_row_0[] = {
    {'!', 1, 9, RenameKeyCharacter}, {'@', 10, 9, RenameKeyCharacter},
    {'#', 19, 9, RenameKeyCharacter}, {'$', 28, 9, RenameKeyCharacter},
    {'%', 37, 9, RenameKeyCharacter}, {'&', 46, 9, RenameKeyCharacter},
    {'(', 55, 9, RenameKeyCharacter}, {')', 64, 9, RenameKeyCharacter},
    {'+', 73, 9, RenameKeyCharacter}, {'=', 82, 9, RenameKeyCharacter},
    {'0', 92, 9, RenameKeyCharacter}, {'1', 102, 9, RenameKeyCharacter},
    {'2', 111, 9, RenameKeyCharacter}, {'3', 120, 9, RenameKeyCharacter},
};

static const RenameKey rename_symbol_row_1[] = {
    {'~', 1, 9, RenameKeyCharacter}, {';', 10, 9, RenameKeyCharacter},
    {',', 19, 9, RenameKeyCharacter}, {'.', 28, 9, RenameKeyCharacter},
    {'[', 37, 9, RenameKeyCharacter}, {']', 46, 9, RenameKeyCharacter},
    {'{', 55, 9, RenameKeyCharacter}, {'}', 64, 9, RenameKeyCharacter},
    {0, 73, 26, RenameKeyBackspace}, {'4', 102, 9, RenameKeyCharacter},
    {'5', 111, 9, RenameKeyCharacter}, {'6', 120, 9, RenameKeyCharacter},
};

static const RenameKey rename_symbol_row_2[] = {
    {0, 0, 11, RenameKeySymbols}, {'-', 13, 9, RenameKeyCharacter},
    {'_', 21, 9, RenameKeyCharacter}, {'.', 29, 9, RenameKeyCharacter},
    {'@', 37, 9, RenameKeyCharacter}, {'!', 45, 9, RenameKeyCharacter},
    {'&', 53, 9, RenameKeyCharacter}, {'\'', 61, 9, RenameKeyCharacter},
    {0, 69, 14, RenameKeySpace}, {0, 84, 17, RenameKeySave},
    {'7', 102, 9, RenameKeyCharacter}, {'8', 111, 9, RenameKeyCharacter},
    {'9', 120, 9, RenameKeyCharacter},
};

static const RenameKey* const rename_alpha_rows[] = {
    rename_alpha_row_0, rename_alpha_row_1, rename_alpha_row_2};
static const uint8_t rename_alpha_row_sizes[] = {
    COUNT_OF(rename_alpha_row_0), COUNT_OF(rename_alpha_row_1), COUNT_OF(rename_alpha_row_2)};
static const RenameKey* const rename_symbol_rows[] = {
    rename_symbol_row_0, rename_symbol_row_1, rename_symbol_row_2};
static const uint8_t rename_symbol_row_sizes[] = {
    COUNT_OF(rename_symbol_row_0), COUNT_OF(rename_symbol_row_1), COUNT_OF(rename_symbol_row_2)};

static const RenameKey* get_rename_keyboard_row(const CivSwitchApp* app, uint8_t row, uint8_t* size) {
    if(app->rename_symbols) {
        *size = rename_symbol_row_sizes[row];
        return rename_symbol_rows[row];
    }
    *size = rename_alpha_row_sizes[row];
    return rename_alpha_rows[row];
}

static CivSwitchApp* active_app;
static bool keyboard_protocol_boot;

static bool finish_live_recording(CivSwitchApp* app);
static SwitchReport neutral_gamepad(void);

static void wake_tx_waiters(CivSwitchApp* app) {
    if(app->gamepad_tx_sem) furi_semaphore_release(app->gamepad_tx_sem);
    if(app->keyboard_tx_sem) furi_semaphore_release(app->keyboard_tx_sem);
}

static void record_tx_failure(CivSwitchApp* app, TxFailure failure, uint8_t ep, int32_t result) {
    if(app->tx_failure == TxFailureNone) {
        app->tx_failure = failure;
        app->failed_endpoint = ep;
        app->usb_result = result;
    }
}

static uint8_t switch_report_descriptor[] = {
    0x05,0x01,0x09,0x05,0xA1,0x01,0x15,0x00,0x25,0x01,0x35,0x00,0x45,0x01,
    0x75,0x01,0x95,0x10,0x05,0x09,0x19,0x01,0x29,0x10,0x81,0x02,0x05,0x01,
    0x25,0x07,0x46,0x3B,0x01,0x75,0x04,0x95,0x01,0x65,0x14,0x09,0x39,0x81,
    0x42,0x65,0x00,0x95,0x01,0x81,0x01,0x26,0xFF,0x00,0x46,0xFF,0x00,0x09,
    0x30,0x09,0x31,0x09,0x32,0x09,0x35,0x75,0x08,0x95,0x04,0x81,0x02,0x06,
    0x00,0xFF,0x09,0x20,0x95,0x01,0x81,0x02,0x0A,0x21,0x26,0x95,0x08,0x91,
    0x02,0xC0,
};

static uint8_t keyboard_report_descriptor[] = {
    0x05,0x01,0x09,0x06,0xA1,0x01,0x75,0x01,0x95,0x08,0x05,0x07,0x19,0xE0,
    0x29,0xE7,0x15,0x00,0x25,0x01,0x81,0x02,0x95,0x01,0x75,0x08,0x81,0x01,
    0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,0x95,0x01,
    0x75,0x03,0x91,0x01,0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,
    0x19,0x00,0x29,0x65,0x81,0x00,0xC0,
};

static uint8_t composite_config_descriptor[] = {
    0x09,0x02,0x49,0x00,0x02,0x01,0x00,0x80,0xFA,
    0x09,0x04,0x00,0x00,0x02,0x03,0x00,0x00,0x00,
    0x09,0x21,0x11,0x01,0x00,0x01,0x22,0x56,0x00,
    0x07,0x05,0x02,0x03,0x40,0x00,0x01,
    0x07,0x05,0x81,0x03,0x40,0x00,0x01,
    0x09,0x04,0x01,0x00,0x02,0x03,0x01,0x01,0x00,
    0x09,0x21,0x11,0x01,0x00,0x01,0x22,0x3F,0x00,
    0x07,0x05,0x83,0x03,0x08,0x00,0x01,
    0x07,0x05,0x03,0x03,0x08,0x00,0x01,
};

static struct usb_device_descriptor device_descriptor = {
    .bLength = sizeof(struct usb_device_descriptor),
    .bDescriptorType = USB_DTYPE_DEVICE,
    .bcdUSB = VERSION_BCD(2, 0, 0),
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = 8,
    .idVendor = 0x0F0D,
    .idProduct = 0x0092,
    .bcdDevice = VERSION_BCD(1, 0, 0),
    .iManufacturer = 0,
    .iProduct = 0,
    .iSerialNumber = 0,
    .bNumConfigurations = 1,
};

static void endpoint_callback(usbd_device* dev, uint8_t event, uint8_t ep) {
    if(event == usbd_evt_eptx && active_app) {
        const uint8_t endpoint_number = ep & 0x7F;
        if(endpoint_number == (EP_GAMEPAD_IN & 0x7F) && active_app->gamepad_tx_sem) {
            furi_semaphore_release(active_app->gamepad_tx_sem);
        } else if(endpoint_number == (EP_KEYBOARD_IN & 0x7F) && active_app->keyboard_tx_sem) {
            furi_semaphore_release(active_app->keyboard_tx_sem);
        }
    } else if(event == usbd_evt_eprx) {
        uint8_t discard[HID_PACKET_SIZE];
        usbd_ep_read(dev, ep, discard, sizeof(discard));
    }
}

static usbd_respond configure_endpoints(usbd_device* dev, uint8_t cfg) {
    const uint8_t eps[] = {EP_GAMEPAD_IN, EP_GAMEPAD_OUT, EP_KEYBOARD_IN, EP_KEYBOARD_OUT};
    if(cfg == 0) {
        if(active_app) {
            active_app->configured = false;
            active_app->connected = false;
            active_app->controller_wake_pending = false;
            wake_tx_waiters(active_app);
        }
        for(size_t i = 0; i < COUNT_OF(eps); i++) {
            usbd_ep_deconfig(dev, eps[i]);
            usbd_reg_endpoint(dev, eps[i], NULL);
        }
        return usbd_ack;
    }
    if(cfg != 1) return usbd_fail;
    const uint8_t endpoint_sizes[] = {64, 64, HID_PACKET_SIZE, HID_PACKET_SIZE};
    for(size_t i = 0; i < COUNT_OF(eps); i++) {
        if(!usbd_ep_config(dev, eps[i], USB_EPTYPE_INTERRUPT, endpoint_sizes[i])) {
            if(active_app) {
                record_tx_failure(active_app, TxFailureEndpointConfig, eps[i], 0);
                active_app->state = RunError;
            }
            for(size_t configured = 0; configured < i; configured++) {
                usbd_ep_deconfig(dev, eps[configured]);
                usbd_reg_endpoint(dev, eps[configured], NULL);
            }
            return usbd_fail;
        }
    }
    for(size_t i = 0; i < COUNT_OF(eps); i++) usbd_reg_endpoint(dev, eps[i], endpoint_callback);
    // Do not queue ZLPs on the HID IN endpoints: the first real report uses the
    // initial semaphore token and must be the first transfer on each endpoint.
    if(active_app) {
        active_app->configured = true;
        active_app->connected = true;
        active_app->controller_wake_pending = true;
    }
    return usbd_ack;
}

static usbd_respond control_request(usbd_device* dev, usbd_ctlreq* req, usbd_rqc_callback* callback) {
    UNUSED(callback);
    const uint8_t recipient_type = req->bmRequestType & (USB_REQ_RECIPIENT | USB_REQ_TYPE);
    if(recipient_type == (USB_REQ_INTERFACE | USB_REQ_CLASS) && req->wIndex == 0) {
        switch(req->bRequest) {
        case USB_HID_SETIDLE: return usbd_ack;
        case USB_HID_GETREPORT: {
            static const SwitchReport released = {.hat = SWITCH_HAT_NONE, .lx = 0x80, .ly = 0x80, .rx = 0x80, .ry = 0x80};
            dev->status.data_ptr = (uint8_t*)&released;
            dev->status.data_count = sizeof(released);
            return usbd_ack;
        }
        default: return usbd_fail;
        }
    }
    if(recipient_type == (USB_REQ_INTERFACE | USB_REQ_CLASS) && req->wIndex == 1) {
        switch(req->bRequest) {
        case USB_HID_SETIDLE: return usbd_ack;
        case USB_HID_SETPROTOCOL:
            if(req->wValue > 1) return usbd_fail;
            keyboard_protocol_boot = req->wValue == 0;
            return usbd_ack;
        case USB_HID_GETREPORT: {
            static const KeyboardReport released = {0};
            dev->status.data_ptr = (uint8_t*)&released;
            dev->status.data_count = sizeof(released);
            return usbd_ack;
        }
        default: return usbd_fail;
        }
    }
    if(recipient_type == (USB_REQ_INTERFACE | USB_REQ_STANDARD) && req->bRequest == USB_STD_GET_DESCRIPTOR) {
        const uint8_t descriptor_type = req->wValue >> 8;
        if(descriptor_type == USB_DTYPE_HID) {
            if(req->wIndex == 0) {
                dev->status.data_ptr = composite_config_descriptor + 18;
                dev->status.data_count = 9;
                return usbd_ack;
            }
            if(req->wIndex == 1) {
                dev->status.data_ptr = composite_config_descriptor + 50;
                dev->status.data_count = 9;
                return usbd_ack;
            }
        } else if(descriptor_type == USB_DTYPE_HID_REPORT) {
            if(req->wIndex == 0) {
                dev->status.data_ptr = switch_report_descriptor;
                dev->status.data_count = sizeof(switch_report_descriptor);
                return usbd_ack;
            }
            if(req->wIndex == 1) {
                dev->status.data_ptr = keyboard_report_descriptor;
                dev->status.data_count = sizeof(keyboard_report_descriptor);
                return usbd_ack;
            }
        }
    }
    return usbd_fail;
}

static void usb_deinit(usbd_device* dev) {
    usbd_reg_config(dev, NULL);
    usbd_reg_control(dev, NULL);
}

static void usb_suspend(usbd_device* dev) {
    UNUSED(dev);
    if(active_app) {
        active_app->connected = false;
        wake_tx_waiters(active_app);
    }
}

static void usb_wakeup(usbd_device* dev) {
    UNUSED(dev);
    if(active_app) {
        active_app->connected = active_app->configured;
        if(active_app->configured) active_app->controller_wake_pending = true;
    }
}

static void usb_state_callback(FuriHalUsbStateEvent event, void* ctx) {
    CivSwitchApp* app = ctx;
    if(event == FuriHalUsbStateEventReset) {
        app->connected = false;
        app->configured = false;
        app->controller_wake_pending = false;
        wake_tx_waiters(app);
    } else if(event == FuriHalUsbStateEventWakeup) {
        app->connected = app->configured;
        if(app->configured) app->controller_wake_pending = true;
    } else if(event == FuriHalUsbStateEventSuspend) {
        app->connected = false;
        app->controller_wake_pending = false;
        wake_tx_waiters(app);
    }
}

static void usb_init(usbd_device* dev, FuriHalUsbInterface* intf, void* ctx) {
    UNUSED(intf);
    UNUSED(ctx);
    // Match Flipper's native HID driver: one initial token allows the first
    // report; endpoint completion releases the token for each following one.
    if(active_app && !active_app->gamepad_tx_sem) active_app->gamepad_tx_sem = furi_semaphore_alloc(1, 1);
    if(active_app && !active_app->keyboard_tx_sem) active_app->keyboard_tx_sem = furi_semaphore_alloc(1, 1);
    if(active_app) active_app->usb_dev = dev;
    usbd_reg_config(dev, configure_endpoints);
    usbd_reg_control(dev, control_request);
    usbd_connect(dev, true);
}

static FuriHalUsbInterface civswitch_usb = {
    .init = usb_init,
    .deinit = usb_deinit,
    .wakeup = usb_wakeup,
    .suspend = usb_suspend,
    .dev_descr = &device_descriptor,
    .str_manuf_descr = NULL,
    .str_prod_descr = NULL,
    .str_serial_descr = NULL,
    .cfg_descr = composite_config_descriptor,
};

static const char* macro_load_error_text(MacroLoadError error);

static const char* tx_failure_text(TxFailure failure) {
    switch(failure) {
    case TxFailureDisconnected: return "USB disconnected";
    case TxFailureBusyTimeout: return "USB busy timeout";
    case TxFailureEndpointConfig: return "USB config failed";
    case TxFailureWriteRejected: return "USB write rejected";
    case TxFailureAckTimeout: return "USB ACK timeout";
    case TxFailureNone: return "USB ready";
    }
    return "USB error";
}

static void draw_macro_filename_at(Canvas* canvas, uint8_t x, const char* name, int16_t y) {
    char line[MACRO_FILENAME_MAX];
    snprintf(line, sizeof(line), "%s", name);
    const uint8_t available_width = canvas_width(canvas) - x - 4U;
    const uint8_t ellipsis_width = canvas_string_width(canvas, "...");
    size_t length = strlen(line);
    bool truncated = false;
    while(length > 0U &&
          (length > sizeof(line) - 4U ||
           canvas_string_width(canvas, line) + ellipsis_width > available_width)) {
        line[--length] = '\0';
        truncated = true;
    }
    if(truncated) memcpy(line + length, "...", 4U);
    canvas_draw_str(canvas, x, y, line);
}

static void draw_macro_filename(Canvas* canvas, const char* name, int16_t y) {
    draw_macro_filename_at(canvas, 4U, name, y);
}

static void normalize_macro_list_offset(CivSwitchApp* app) {
    if(app->macro_count <= MACRO_LIST_VISIBLE) {
        app->macro_list_offset = 0U;
        return;
    }

    if(app->macro_index < app->macro_list_offset) {
        app->macro_list_offset = app->macro_index;
    } else if(app->macro_index >= app->macro_list_offset + MACRO_LIST_VISIBLE) {
        app->macro_list_offset = app->macro_index - MACRO_LIST_VISIBLE + 1U;
    }

    const uint8_t max_offset = app->macro_count - MACRO_LIST_VISIBLE;
    if(app->macro_list_offset > max_offset) app->macro_list_offset = max_offset;
}

static void format_macro_duration(uint32_t duration_ms, char* output, size_t capacity) {
    const uint32_t total_seconds = duration_ms / 1000U;
    const uint32_t hours = total_seconds / 3600U;
    const uint32_t minutes = (total_seconds / 60U) % 60U;
    const uint32_t seconds = total_seconds % 60U;
    if(hours) {
        snprintf(
            output,
            capacity,
            "%lu:%02lu:%02lu",
            (unsigned long)hours,
            (unsigned long)minutes,
            (unsigned long)seconds);
    } else {
        snprintf(output, capacity, "%02lu:%02lu", (unsigned long)minutes, (unsigned long)seconds);
    }
}

static void draw_macro_progress(Canvas* canvas, uint8_t x, uint8_t y, uint8_t width, uint32_t elapsed, uint32_t total) {
    canvas_draw_frame(canvas, x, y, width, 8);
    if(total == 0U) return;
    if(elapsed > total) elapsed = total;
    const uint8_t fill_width = (uint8_t)(((uint64_t)elapsed * (width - 2U)) / total);
    if(fill_width) canvas_draw_box(canvas, x + 1U, y + 1U, fill_width, 6);
}

static void draw_header(Canvas* canvas, const char* title, const char* badge) {
    canvas_draw_box(canvas, 0, 0, 128, 12);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 4, 9, title);
    if(badge && badge[0]) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 96, 9, badge);
    }
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
}

static bool live_report_has_button(const SwitchReport* report, uint16_t mask) {
    return (report->buttons & mask) != 0U;
}

static int8_t live_stick_offset(uint8_t value) {
    const int16_t offset = ((int16_t)value - 128) / 24;
    if(offset < -5) return -5;
    if(offset > 5) return 5;
    return (int8_t)offset;
}

static void draw_live_stick(
    Canvas* canvas,
    uint8_t center_x,
    uint8_t center_y,
    uint8_t horizontal,
    uint8_t vertical,
    bool pressed) {
    const int32_t dot_x = center_x + live_stick_offset(horizontal);
    const int32_t dot_y = center_y + live_stick_offset(vertical);
    canvas_draw_circle(canvas, center_x, center_y, 9U);
    canvas_draw_circle(canvas, center_x, center_y, 6U);
    canvas_draw_disc(canvas, dot_x, dot_y, pressed ? 3U : 2U);
}

static void draw_controller_pill(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* label,
    bool pressed) {
    if(pressed) canvas_draw_rbox(canvas, x, y, width, height, 3U);
    else canvas_draw_rframe(canvas, x, y, width, height, 3U);
    canvas_set_color(canvas, pressed ? ColorWhite : ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    const uint8_t label_width = canvas_string_width(canvas, label);
    const uint8_t label_x = x + (width > label_width ? (width - label_width) / 2U : 0U);
    canvas_draw_str(canvas, label_x, y + height - 1U, label);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_controller_round_button(
    Canvas* canvas,
    uint8_t center_x,
    uint8_t center_y,
    uint8_t radius,
    const char* label,
    bool pressed) {
    if(pressed) {
        canvas_draw_disc(canvas, center_x, center_y, radius);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_circle(canvas, center_x, center_y, radius);
        canvas_set_color(canvas, ColorBlack);
    }
    canvas_set_font(canvas, FontSecondary);
    const uint8_t label_width = canvas_string_width(canvas, label);
    canvas_draw_str(canvas, center_x - label_width / 2U, center_y + 2U, label);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_controller_dpad(Canvas* canvas, const SwitchReport* report) {
    const bool hat_up = report->hat == 0U || report->hat == 1U || report->hat == 7U;
    const bool hat_right = report->hat == 1U || report->hat == 2U || report->hat == 3U;
    const bool hat_down = report->hat == 3U || report->hat == 4U || report->hat == 5U;
    const bool hat_left = report->hat == 5U || report->hat == 6U || report->hat == 7U;
    canvas_draw_rbox(canvas, 39, 26, 9, 23, 3U);
    canvas_draw_rbox(canvas, 31, 33, 25, 9, 3U);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_rbox(canvas, 40, 27, 7, 21, 2U);
    canvas_draw_rbox(canvas, 32, 34, 23, 7, 2U);
    canvas_set_color(canvas, ColorBlack);

    if(hat_up) canvas_draw_disc(canvas, 43, 30, 2U);
    if(hat_left) canvas_draw_disc(canvas, 35, 37, 2U);
    if(hat_right) canvas_draw_disc(canvas, 51, 37, 2U);
    if(hat_down) canvas_draw_disc(canvas, 43, 45, 2U);
}

static void draw_live_controller_icon(Canvas* canvas, const SwitchReport* report) {
    static const int16_t outline[][2] = {
        {25, 7},  {18, 9},  {12, 14}, {8, 21},   {5, 30},   {3, 40},   {4, 49},
        {8, 56},  {13, 61}, {19, 62}, {24, 58},  {29, 53},  {40, 49},  {88, 49},
        {99, 53}, {104, 58},{109, 62},{115, 61}, {120, 56}, {124, 49}, {125, 40},
        {123, 30},{120, 21},{116, 14},{110, 9},  {103, 7},  {25, 7},
    };
    canvas_set_color(canvas, ColorBlack);
    for(size_t i = 0U; i + 1U < COUNT_OF(outline); i++) {
        canvas_draw_line(
            canvas,
            outline[i][0],
            outline[i][1],
            outline[i + 1U][0],
            outline[i + 1U][1]);
    }

    canvas_draw_line(canvas, 42, 12, 51, 20);
    canvas_draw_line(canvas, 51, 20, 77, 20);
    canvas_draw_line(canvas, 77, 20, 86, 12);

    draw_controller_pill(canvas, 19, 5, 15, 8, "L", live_report_has_button(report, SWITCH_BUTTON_L));
    draw_controller_pill(canvas, 36, 5, 18, 8, "ZL", live_report_has_button(report, SWITCH_BUTTON_ZL));
    draw_controller_pill(canvas, 74, 5, 18, 8, "ZR", live_report_has_button(report, SWITCH_BUTTON_ZR));
    draw_controller_pill(canvas, 94, 5, 15, 8, "R", live_report_has_button(report, SWITCH_BUTTON_R));

    draw_live_stick(canvas, 27, 23, report->lx, report->ly, false);
    draw_controller_pill(canvas, 34U, 19U, 15U, 8U, "L3", live_report_has_button(report, SWITCH_BUTTON_L3));
    draw_controller_dpad(canvas, report);
    draw_live_stick(canvas, 82, 37, report->rx, report->ry, false);
    draw_controller_pill(canvas, 88U, 44U, 15U, 8U, "R3", live_report_has_button(report, SWITCH_BUTTON_R3));

    draw_controller_round_button(canvas, 64, 15, 5U, "H", live_report_has_button(report, SWITCH_BUTTON_HOME));
    draw_controller_round_button(canvas, 57, 23, 4U, "-", live_report_has_button(report, SWITCH_BUTTON_MINUS));
    draw_controller_round_button(canvas, 72, 23, 4U, "+", live_report_has_button(report, SWITCH_BUTTON_PLUS));
    draw_controller_round_button(canvas, 64, 30, 3U, "C", live_report_has_button(report, SWITCH_BUTTON_CAPTURE));

    draw_controller_round_button(canvas, 109, 22, 5U, "X", live_report_has_button(report, SWITCH_BUTTON_X));
    draw_controller_round_button(canvas, 99, 33, 5U, "Y", live_report_has_button(report, SWITCH_BUTTON_Y));
    draw_controller_round_button(canvas, 119, 33, 5U, "A", live_report_has_button(report, SWITCH_BUTTON_A));
    draw_controller_round_button(canvas, 109, 44, 5U, "B", live_report_has_button(report, SWITCH_BUTTON_B));
}

static void draw_simulator_focus(Canvas* canvas, uint8_t selection, bool stick_active) {
    if(selection >= SimulatorControlCount) return;
    const SimulatorControl* control = &simulator_controls[selection];
    if(selection <= SimulatorControlR) {
        canvas_draw_rframe(canvas, control->x - 9U, control->y - 6U, 18U, 13U, 3U);
    } else if(selection == SimulatorControlLeftStick || selection == SimulatorControlRightStick) {
        canvas_draw_circle(canvas, control->x, control->y, 11U);
        if(stick_active) canvas_draw_circle(canvas, control->x, control->y, 13U);
    } else if(selection == SimulatorControlL3 || selection == SimulatorControlR3) {
        canvas_draw_rframe(canvas, control->x - 9U, control->y - 5U, 18U, 10U, 3U);
    } else if(selection >= SimulatorControlDpadUp && selection <= SimulatorControlDpadRight) {
        canvas_draw_circle(canvas, control->x, control->y, 4U);
    } else {
        canvas_draw_circle(canvas, control->x, control->y, 7U);
    }
}

static void draw_simulator_ui(Canvas* canvas, const CivSwitchApp* app) {
    draw_live_controller_icon(canvas, &app->simulator_report);
    draw_simulator_focus(canvas, app->simulator_selection, app->simulator_stick_active);
    if(!app->connected || app->simulator_error) {
        canvas_draw_box(canvas, 0, 54, 128, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 35, 62, app->connected ? "USB SEND ERROR" : "USB DISCONNECTED");
        canvas_set_color(canvas, ColorBlack);
    }
}

static void get_live_display_report(const CivSwitchApp* app, SwitchReport* report) {
    *report = neutral_gamepad();
    if(!app->live_record_mutex ||
       furi_mutex_acquire(app->live_record_mutex, 0U) != FuriStatusOk) {
        return;
    }
    if(app->live_latest_valid) *report = app->live_latest_state;
    furi_mutex_release(app->live_record_mutex);
}

static void draw_live_controller(Canvas* canvas, const CivSwitchApp* app) {
    SwitchReport report;
    get_live_display_report(app, &report);
    draw_live_controller_icon(canvas, &report);
}

static void draw_connection_row(Canvas* canvas, uint8_t y, const char* label, const char* value, bool connected) {
    if(connected) canvas_draw_box(canvas, 4, y - 3U, 3, 3);
    else canvas_draw_frame(canvas, 4, y - 3U, 3, 3);
    canvas_draw_str(canvas, 11, y, label);
    canvas_draw_str(canvas, 47, y, value);
}

static void draw_live_menu(Canvas* canvas, const CivSwitchApp* app) {
    draw_header(
        canvas,
        "LIVE RELAY",
        app->tx_failure != TxFailureNone ? "ERR" : (app->live_recording ? "REC" : "LIVE"));
    draw_connection_row(canvas, 21U, "USB", app->connected ? "CONNECTED" : "WAITING", app->connected);
    draw_connection_row(
        canvas,
        29U,
        "BLE",
        app->bt_status == BtStatusConnected ? "CONNECTED" : "WAITING",
        app->bt_status == BtStatusConnected);

    const uint8_t y = 43U;
    canvas_draw_box(canvas, 4, y, 120, 15);
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_frame(canvas, 9, y + 4U, 7, 7);
    canvas_draw_line(canvas, 11, y + 3U, 14, y + 3U);
    canvas_draw_str(canvas, 23, y + 10U, "RECORDER");
    canvas_draw_str(canvas, 116, y + 10U, ">");
    canvas_set_color(canvas, ColorBlack);
}

static void draw_live_recorder(Canvas* canvas, const CivSwitchApp* app) {
    char line[28];
    char elapsed[12];
    const uint32_t record_elapsed = app->live_recording ?
                                        furi_get_tick() - app->live_record_started_tick :
                                        app->live_record_elapsed_ms;
    format_macro_duration(record_elapsed, elapsed, sizeof(elapsed));

    if(app->live_recording) {
        snprintf(line, sizeof(line), "RECORDING INPUT");
    } else if(app->live_record_full) {
        snprintf(line, sizeof(line), "STORAGE LIMIT REACHED");
    } else if(app->live_record_saved) {
        snprintf(line, sizeof(line), "SAVED %.18s", app->live_record_saved_name);
    } else if(app->ui_message[0]) {
        snprintf(line, sizeof(line), "%.22s", app->ui_message);
    } else {
        snprintf(line, sizeof(line), "READY TO RECORD");
    }

    draw_header(
        canvas,
        "LIVE RECORDER",
        app->live_recording ? "REC" :
            (app->live_record_full ? "FULL" : (app->live_record_saved ? "SAVED" : "READY")));
    canvas_draw_str(canvas, 5, 22, line);

    canvas_draw_frame(canvas, 4, 25, 58, 21);
    canvas_draw_frame(canvas, 66, 25, 58, 21);
    canvas_draw_str(canvas, 9, 33, "DURATION");
    canvas_draw_str(canvas, 71, 33, "STEPS");
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 9, 43, elapsed);
    snprintf(line, sizeof(line), "%u", (unsigned)app->live_record_steps);
    canvas_draw_str(canvas, 71, 43, line);
    canvas_set_font(canvas, FontSecondary);

    for(uint8_t i = 0U; i < 2U; i++) {
        const uint8_t y = i == 0U ? 47U : 56U;
        const bool selected = i == app->live_recorder_selection;
        const char* label = i == 0U ?
                                (app->live_recording || app->live_record_full ? "STOP AND SAVE" :
                                     (app->live_record_saved ? "RECORD AGAIN" : "START RECORDING")) :
                                "VIEW CONTROLLER";
        if(selected) canvas_draw_box(canvas, 2, y, 124, 8);
        else canvas_draw_frame(canvas, 2, y, 124, 8);
        canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
        canvas_draw_str(canvas, 8, y + 7U, label);
        canvas_draw_str(canvas, 116, y + 7U, ">");
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_rename_ui(Canvas* canvas, const CivSwitchApp* app) {
    char preview[MACRO_BASENAME_MAX + 2U];
    const size_t length = strlen(app->rename_name);
    const size_t cursor = app->rename_position > length ? length : app->rename_position;
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    memcpy(preview, app->rename_name, cursor);
    preview[cursor] = '|';
    memcpy(preview + cursor + 1U, app->rename_name + cursor, length - cursor + 1U);

    canvas_draw_str(canvas, 3, 8, app->ui_message[0] ? app->ui_message : "RENAME MACRO");
    canvas_draw_str(canvas, 104, 8, app->rename_symbols ? "SYM" : "ABC");
    canvas_draw_frame(canvas, 2, 11, 124, 14);
    canvas_draw_str(canvas, 6, 21, preview);

    canvas_set_font(canvas, FontKeyboard);
    for(uint8_t row = 0U; row < 3U; row++) {
        uint8_t key_count;
        const RenameKey* keys = get_rename_keyboard_row(app, row, &key_count);
        const uint8_t baseline = 35U + row * 11U;
        for(uint8_t column = 0U; column < key_count; column++) {
            const RenameKey* key = &keys[column];
            const bool selected = !app->rename_cursor_selected &&
                                  row == app->rename_keyboard_row &&
                                  column == app->rename_keyboard_column;
            const uint8_t frame_x =
                key->action == RenameKeyCharacter && key->x >= 2U ? key->x - 2U : key->x;
            if(selected) {
                canvas_draw_box(canvas, frame_x, baseline - 9U, key->width, 11U);
                canvas_set_color(canvas, ColorWhite);
            } else if(key->action != RenameKeyCharacter) {
                canvas_draw_frame(canvas, frame_x, baseline - 9U, key->width, 11U);
            }

            if(key->action == RenameKeyCharacter) {
                canvas_draw_glyph(canvas, key->x, baseline, key->value);
            } else {
                const char* label = key->action == RenameKeyBackspace ? "DEL" :
                                    key->action == RenameKeySymbols ? "+=#" :
                                    key->action == RenameKeySpace ? "SPC" : "SAVE";
                canvas_set_font(canvas, FontSecondary);
                const uint8_t label_width = canvas_string_width(canvas, label);
                const uint8_t label_x = frame_x + (key->width > label_width ? (key->width - label_width) / 2U : 0U);
                canvas_draw_str(canvas, label_x, baseline - 2U, label);
                canvas_set_font(canvas, FontKeyboard);
            }
            canvas_set_color(canvas, ColorBlack);
        }
    }
    canvas_set_font(canvas, FontSecondary);
}

static void draw_macro_ui(Canvas* canvas, const CivSwitchApp* app) {
    char line[32];
    char duration[12];
    if(app->macro_ui_screen == MacroUiHome) {
        draw_header(canvas, "SWITCH MACRO", app->connected ? "READY" : "WAIT");

        for(uint8_t i = 0U; i < 3U; i++) {
            const uint8_t y = 17U + i * 15U;
            const bool selected = i == app->home_index;
            if(selected) canvas_draw_box(canvas, 4, y, 120, 13);
            else canvas_draw_frame(canvas, 4, y, 120, 13);
            canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
            const char* labels[] = {"Macros", "Live relay", "Simulator"};
            canvas_draw_str(canvas, 10, y + 9U, labels[i]);
            if(i == 0U) {
                snprintf(line, sizeof(line), "%u FILES", (unsigned)app->macro_count);
                canvas_draw_str(canvas, 78, y + 9U, line);
            } else {
                canvas_draw_str(canvas, 101, y + 9U, i == 1U ? "PC" : "USB");
            }
            canvas_set_color(canvas, ColorBlack);
        }
        if(app->live_start_failed) {
            canvas_draw_str(canvas, 5, 63, "Could not start Live");
        } else if(app->ui_message[0]) {
            canvas_draw_str(canvas, 5, 63, app->ui_message);
        }
    } else if(app->macro_ui_screen == MacroUiList) {
        snprintf(line, sizeof(line), "%u/%u", (unsigned)app->macro_index + 1U, (unsigned)app->macro_count);
        draw_header(canvas, "MACRO LIBRARY", app->macro_count ? line : "SD CARD");
        if(app->macro_count == 0U) {
            canvas_draw_frame(canvas, 5, 22, 118, 34);
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str(canvas, 22, 38, "NO MACROS");
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 24, 49, "Add .txt files to SD");
            if(app->ui_message[0]) canvas_draw_str(canvas, 5, 63, app->ui_message);
        } else {
            const uint8_t visible_count = app->macro_count - app->macro_list_offset < MACRO_LIST_VISIBLE ?
                                              app->macro_count - app->macro_list_offset :
                                              MACRO_LIST_VISIBLE;
            for(uint8_t row = 0U; row < visible_count; row++) {
                const uint8_t index = app->macro_list_offset + row;
                const uint8_t y = 14U + row * 10U;
                const bool selected = index == app->macro_index;
                if(selected) canvas_draw_box(canvas, 3, y, 122, 9);
                canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
                draw_macro_filename_at(canvas, 9U, app->macro_files[index].name, y + 7U);
                canvas_set_color(canvas, ColorBlack);
            }

            if(app->load_error != MacroLoadOk) {
                if(app->load_error == MacroLoadBadSyntax) {
                    snprintf(line, sizeof(line), "SYNTAX ERROR  LINE %u", (unsigned)app->load_error_line);
                } else {
                    snprintf(line, sizeof(line), "%s", macro_load_error_text(app->load_error));
                }
                canvas_draw_str(canvas, 5, 61, line);
            } else if(app->ui_message[0]) {
                canvas_draw_str(canvas, 5, 61, app->ui_message);
            } else {
                format_macro_duration(app->macro_total_ms, duration, sizeof(duration));
                snprintf(line, sizeof(line), "STEPS %u   %s", (unsigned)app->step_count, duration);
                canvas_draw_str(canvas, 5, 61, line);
            }
        }
    } else if(app->macro_ui_screen == MacroUiDetails) {
        snprintf(line, sizeof(line), "%u/%u", (unsigned)app->macro_index + 1U, (unsigned)app->macro_count);
        draw_header(canvas, "MACRO DETAILS", line);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 24);

        canvas_draw_frame(canvas, 4, 28, 58, 17);
        canvas_draw_frame(canvas, 66, 28, 58, 17);
        canvas_draw_str(canvas, 9, 35, "DURATION");
        canvas_draw_str(canvas, 72, 35, "STEPS");
        canvas_set_font(canvas, FontPrimary);
        format_macro_duration(app->macro_total_ms, duration, sizeof(duration));
        canvas_draw_str(canvas, 9, 43, duration);
        snprintf(line, sizeof(line), "%u", (unsigned)app->step_count);
        canvas_draw_str(canvas, 72, 43, line);
        canvas_set_font(canvas, FontSecondary);

        if(app->macro_detail_selection == 0U) canvas_draw_box(canvas, 2, 47, 124, 9);
        canvas_set_color(canvas, app->macro_detail_selection == 0U ? ColorWhite : ColorBlack);
        canvas_draw_str(canvas, 8, 54, app->macro_loop ? "LOOP   ON" : "LOOP   OFF");
        canvas_set_color(canvas, ColorBlack);

        if(app->macro_detail_selection == 1U) canvas_draw_box(canvas, 2, 56, 124, 8);
        canvas_set_color(canvas, app->macro_detail_selection == 1U ? ColorWhite : ColorBlack);
        canvas_draw_str(canvas, 8, 63, "PLAY MACRO");
        canvas_set_color(canvas, ColorBlack);
    } else if(app->macro_ui_screen == MacroUiActions) {
        draw_header(canvas, "MACRO OPTIONS", NULL);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 24);
        static const char* actions[] = {"PLAY MACRO", "RENAME FILE", "DELETE FILE"};
        static const char* descriptions[] = {"RUN", "EDIT NAME", "REMOVE"};
        for(uint8_t i = 0U; i < 3U; i++) {
            const uint8_t y = 28U + (uint8_t)(i * 12U);
            const bool selected = i == app->macro_action_index;
            if(selected) canvas_draw_box(canvas, 3, y, 122, 11);
            else canvas_draw_frame(canvas, 3, y, 122, 11);
            canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
            canvas_draw_str(canvas, 9, y + 8U, actions[i]);
            canvas_draw_str(canvas, 88, y + 8U, descriptions[i]);
            canvas_set_color(canvas, ColorBlack);
        }
    } else if(app->macro_ui_screen == MacroUiRename) {
        draw_rename_ui(canvas, app);
    } else if(app->macro_ui_screen == MacroUiDeleteConfirm) {
        draw_header(canvas, "DELETE MACRO?", "!");
        canvas_draw_frame(canvas, 4, 22, 120, 28);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 12, 40, "REMOVE");
        canvas_set_font(canvas, FontSecondary);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 47);
        canvas_draw_box(canvas, 2, 54, 124, 10);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str(canvas, 8, 62, "DELETE PERMANENTLY");
        canvas_set_color(canvas, ColorBlack);
    } else if(app->macro_ui_screen == MacroUiSimulator) {
        draw_simulator_ui(canvas, app);
    }
}

static void app_draw(Canvas* canvas, void* ctx) {
    const CivSwitchApp* app = ctx;
    canvas_clear(canvas);
    char line[32];
    char elapsed[12];
    char total[12];
    switch(app->state) {
    case RunReady:
        draw_macro_ui(canvas, app);
        break;
    case RunSending:
        draw_header(canvas, app->macro_loop ? "MACRO LOOP" : "MACRO PLAYBACK", NULL);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 24);
        if(app->macro_loop) {
            snprintf(
                line,
                sizeof(line),
                "STEP %u/%u   CYCLE %lu",
                (unsigned)(app->completed_steps < app->step_count ? app->completed_steps + 1U : app->step_count),
                (unsigned)app->step_count,
                (unsigned long)app->macro_cycles + 1U);
        } else {
            snprintf(
                line,
                sizeof(line),
                "STEP %u/%u",
                (unsigned)(app->completed_steps < app->step_count ? app->completed_steps + 1U : app->step_count),
                (unsigned)app->step_count);
        }
        canvas_draw_str(canvas, 5, 35, line);
        draw_macro_progress(canvas, 4, 40, 120, app->macro_elapsed_ms, app->macro_total_ms);
        format_macro_duration(app->macro_elapsed_ms, elapsed, sizeof(elapsed));
        format_macro_duration(app->macro_total_ms, total, sizeof(total));
        snprintf(line, sizeof(line), "%s / %s", elapsed, total);
        canvas_draw_str(canvas, 5, 59, line);
        break;
    case RunLive:
        if(app->live_ui_screen == LiveUiMenu) {
            draw_live_menu(canvas, app);
        } else if(app->live_ui_screen == LiveUiRecorder) {
            draw_live_recorder(canvas, app);
        } else {
            draw_live_controller(canvas, app);
        }
        break;
    case RunFinished:
        draw_header(canvas, "PLAY COMPLETE", "DONE");
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 25);
        format_macro_duration(app->macro_total_ms, total, sizeof(total));
        canvas_draw_frame(canvas, 4, 31, 120, 21);
        canvas_draw_str(canvas, 12, 39, "STEPS");
        canvas_set_font(canvas, FontPrimary);
        snprintf(line, sizeof(line), "%u", (unsigned)app->step_count);
        canvas_draw_str(canvas, 12, 49, line);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 74, 39, "DURATION");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 74, 49, total);
        break;
    case RunCanceled:
        draw_header(canvas, "PLAYBACK STOPPED", NULL);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 25);
        format_macro_duration(app->macro_elapsed_ms, elapsed, sizeof(elapsed));
        canvas_draw_frame(canvas, 4, 31, 120, 21);
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 12, 40, "STOPPED AT");
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 12, 49, elapsed);
        break;
    case RunError:
        snprintf(line, sizeof(line), "EP %02X", app->failed_endpoint);
        draw_header(canvas, "USB ERROR", line);
        canvas_draw_str(canvas, 5, 26, tx_failure_text(app->tx_failure));
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 39);
        canvas_draw_frame(canvas, 4, 44, 120, 15);
        snprintf(line, sizeof(line), "STEP %u FAILED", (unsigned)app->completed_steps + 1U);
        canvas_draw_str(canvas, 10, 54, line);
        break;
    }
}

static void app_input(InputEvent* event, void* ctx) {
    CivSwitchApp* app = ctx;
    if(event->type == InputTypePress || event->type == InputTypeRelease ||
       event->type == InputTypeRepeat) {
        furi_message_queue_put(app->input_queue, event, 0);
    }
}

static bool delay_or_cancel(CivSwitchApp* app, uint32_t delay_ms) {
    uint32_t update_elapsed = 0U;
    while(delay_ms && !app->stop_requested) {
        const uint32_t part = delay_ms > 10 ? 10 : delay_ms;
        furi_delay_ms(part);
        delay_ms -= part;
        app->macro_elapsed_ms += part;
        update_elapsed += part;
        if(update_elapsed >= 250U) {
            view_port_update(app->view_port);
            update_elapsed = 0U;
        }
    }
    return !app->stop_requested;
}

static FuriSemaphore* endpoint_semaphore(CivSwitchApp* app, uint8_t ep) {
    return ep == EP_GAMEPAD_IN ? app->gamepad_tx_sem : app->keyboard_tx_sem;
}

static bool send_report(CivSwitchApp* app, uint8_t ep, const void* data, uint16_t len) {
    FuriSemaphore* tx_sem = endpoint_semaphore(app, ep);
    if(!app->connected || !app->usb_dev || !tx_sem) {
        record_tx_failure(app, TxFailureDisconnected, ep, 0);
        return false;
    }
    int32_t write_result = 0;
    for(uint8_t attempt = 0U; attempt < 4U; attempt++) {
        if(furi_semaphore_acquire(tx_sem, 250) != FuriStatusOk) {
            record_tx_failure(app, TxFailureBusyTimeout, ep, 0);
            return false;
        }
        if(!app->connected || !app->usb_dev) {
            furi_semaphore_release(tx_sem);
            record_tx_failure(app, TxFailureDisconnected, ep, 0);
            return false;
        }
        write_result = usbd_ep_write(app->usb_dev, ep, data, len);
        if(write_result == len) return true;
        furi_semaphore_release(tx_sem);
        if(attempt < 3U) furi_delay_ms(1);
    }
    record_tx_failure(app, TxFailureWriteRejected, ep, write_result);
    return false;
}

static bool wait_report_complete(CivSwitchApp* app, uint8_t ep) {
    FuriSemaphore* tx_sem = endpoint_semaphore(app, ep);
    if(!app->connected || !app->usb_dev || !tx_sem) {
        record_tx_failure(app, TxFailureDisconnected, ep, 0);
        return false;
    }
    if(furi_semaphore_acquire(tx_sem, 250) != FuriStatusOk) {
        record_tx_failure(app, TxFailureAckTimeout, ep, 0);
        return false;
    }
    if(!app->connected || !app->usb_dev) {
        furi_semaphore_release(tx_sem);
        record_tx_failure(app, TxFailureDisconnected, ep, 0);
        return false;
    }
    // The completion callback released this token. Consume it to wait for the
    // transfer, then return it so the next mode can submit its first report.
    furi_semaphore_release(tx_sem);
    return true;
}

static SwitchReport neutral_gamepad(void) {
    return (SwitchReport){
        .buttons = 0,
        .hat = SWITCH_HAT_NONE,
        .lx = 0x80,
        .ly = 0x80,
        .rx = 0x80,
        .ry = 0x80,
        .vendor = 0,
    };
}

static bool send_gamepad_report(
    CivSwitchApp* app,
    uint16_t buttons,
    uint8_t hat,
    uint8_t lx,
    uint8_t ly,
    uint8_t rx,
    uint8_t ry) {
    SwitchReport report = neutral_gamepad();
    report.buttons = buttons;
    report.hat = hat;
    report.lx = lx;
    report.ly = ly;
    report.rx = rx;
    report.ry = ry;
    return send_report(app, EP_GAMEPAD_IN, &report, sizeof(report));
}

static bool send_controller_wake_sequence(CivSwitchApp* app) {
    if(!app->connected || !app->configured) return false;
    furi_delay_ms(150U);
    if(!app->connected) return false;

    bool sent = send_gamepad_report(app, 0U, SWITCH_HAT_NONE, 0x80U, 0x80U, 0x80U, 0x80U);
    if(sent) sent = wait_report_complete(app, EP_GAMEPAD_IN);
    if(!sent || !app->connected) return false;

    sent = send_gamepad_report(
        app, SWITCH_BUTTON_A, SWITCH_HAT_NONE, 0x80U, 0x80U, 0x80U, 0x80U);
    if(sent) sent = wait_report_complete(app, EP_GAMEPAD_IN);
    if(!sent || !app->connected) return false;

    furi_delay_ms(60U);
    if(!app->connected) return false;

    sent = send_gamepad_report(app, 0U, SWITCH_HAT_NONE, 0x80U, 0x80U, 0x80U, 0x80U);
    if(sent) wait_report_complete(app, EP_GAMEPAD_IN);
    return sent && app->connected;
}

static void service_controller_wake(CivSwitchApp* app) {
    if(!app->controller_wake_pending || !app->connected || !app->configured ||
       app->state != RunReady || app->live_active || app->worker) {
        return;
    }

    app->controller_wake_pending = false;
    send_controller_wake_sequence(app);
}

static void live_queue_report(CivSwitchApp* app, const SwitchReport* report) {
    if(!app->live_queue) return;
    if(furi_message_queue_put(app->live_queue, report, 0) != FuriStatusOk) {
        SwitchReport discarded;
        if(furi_message_queue_get(app->live_queue, &discarded, 0) == FuriStatusOk) {
            app->live_frames_dropped++;
        }
        if(furi_message_queue_put(app->live_queue, report, 0) != FuriStatusOk) {
            app->live_frames_dropped++;
        }
    }
}

static bool live_report_equal(const SwitchReport* left, const SwitchReport* right) {
    return left->buttons == right->buttons && left->hat == right->hat && left->lx == right->lx &&
           left->ly == right->ly && left->rx == right->rx && left->ry == right->ry;
}

static const char* live_hat_name(uint8_t hat) {
    switch(hat) {
    case 0x00: return "UP";
    case 0x01: return "UP_RIGHT";
    case 0x02: return "RIGHT";
    case 0x03: return "DOWN_RIGHT";
    case 0x04: return "DOWN";
    case 0x05: return "DOWN_LEFT";
    case 0x06: return "LEFT";
    case 0x07: return "UP_LEFT";
    default: return NULL;
    }
}

static bool live_record_add_token(char* controls, size_t capacity, size_t* length, const char* token) {
    const int written = snprintf(controls + *length, capacity - *length, "%s%s", *length ? "+" : "", token);
    if(written < 0 || (size_t)written >= capacity - *length) return false;
    *length += (size_t)written;
    return true;
}

static bool live_record_append_step_locked(CivSwitchApp* app, const SwitchReport* report, uint32_t duration_ms) {
    if(!app->live_record_text) return false;
    if(duration_ms == 0U) duration_ms = 1U;
    while(duration_ms > 0U) {
        const uint32_t part = duration_ms > MACRO_MAX_STEP_MS ? MACRO_MAX_STEP_MS : duration_ms;
        if(app->live_record_steps >= MACRO_MAX_STEPS) {
            app->live_record_full = true;
            app->live_recording = false;
            return false;
        }

        char controls[104] = {0};
        size_t controls_length = 0U;
        static const struct {
            uint16_t mask;
            const char* name;
        } buttons[] = {
            {SWITCH_BUTTON_A, "A"}, {SWITCH_BUTTON_B, "B"}, {SWITCH_BUTTON_X, "X"},
            {SWITCH_BUTTON_Y, "Y"}, {SWITCH_BUTTON_L, "L"}, {SWITCH_BUTTON_R, "R"},
            {SWITCH_BUTTON_ZL, "ZL"}, {SWITCH_BUTTON_ZR, "ZR"}, {SWITCH_BUTTON_MINUS, "MINUS"},
            {SWITCH_BUTTON_PLUS, "PLUS"}, {SWITCH_BUTTON_L3, "L3"}, {SWITCH_BUTTON_R3, "R3"},
            {SWITCH_BUTTON_HOME, "HOME"}, {SWITCH_BUTTON_CAPTURE, "CAPTURE"},
        };
        for(size_t i = 0U; i < COUNT_OF(buttons); i++) {
            if((report->buttons & buttons[i].mask) &&
               !live_record_add_token(controls, sizeof(controls), &controls_length, buttons[i].name)) {
                app->live_record_full = true;
                app->live_recording = false;
                return false;
            }
        }
        const char* hat_name = live_hat_name(report->hat);
        if(hat_name && !live_record_add_token(controls, sizeof(controls), &controls_length, hat_name)) {
            app->live_record_full = true;
            app->live_recording = false;
            return false;
        }
        const uint8_t axes[] = {report->lx, report->ly, report->rx, report->ry};
        static const char* axis_names[] = {"LX", "LY", "RX", "RY"};
        for(size_t i = 0U; i < COUNT_OF(axes); i++) {
            if(axes[i] != 0x80U) {
                char axis[8];
                snprintf(axis, sizeof(axis), "%s:%u", axis_names[i], (unsigned)axes[i]);
                if(!live_record_add_token(controls, sizeof(controls), &controls_length, axis)) {
                    app->live_record_full = true;
                    app->live_recording = false;
                    return false;
                }
            }
        }
        if(controls_length == 0U) snprintf(controls, sizeof(controls), "NONE");

        char line[128];
        const int line_length = snprintf(line, sizeof(line), "STATE %s %lu\n", controls, (unsigned long)part);
        if(line_length < 0 || (size_t)line_length >= sizeof(line) ||
           (size_t)line_length > MACRO_FILE_MAX_BYTES - app->live_record_bytes) {
            app->live_record_full = true;
            app->live_recording = false;
            return false;
        }
        memcpy(app->live_record_text + app->live_record_bytes, line, (size_t)line_length);
        app->live_record_bytes += (uint16_t)line_length;
        app->live_record_text[app->live_record_bytes] = '\0';
        app->live_record_steps++;
        duration_ms -= part;
    }
    return true;
}

static void live_record_state(CivSwitchApp* app, const SwitchReport* report, uint32_t tick) {
    if(!app->live_record_mutex || furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) != FuriStatusOk) return;
    app->live_latest_state = *report;
    app->live_latest_valid = true;
    if(app->live_recording && app->live_record_text) {
        app->live_record_elapsed_ms = tick - app->live_record_started_tick;
        if(!app->live_record_has_state) {
            app->live_record_state = *report;
            app->live_record_last_tick = tick;
            app->live_record_has_state = true;
        } else if(!live_report_equal(&app->live_record_state, report)) {
            const uint32_t elapsed = tick - app->live_record_last_tick;
            if(live_record_append_step_locked(app, &app->live_record_state, elapsed)) {
                app->live_record_state = *report;
                app->live_record_last_tick = tick;
            }
        }
    }
    furi_mutex_release(app->live_record_mutex);
}

static uint16_t live_serial_event_callback(SerialServiceEvent event, void* context) {
    CivSwitchApp* app = context;
    if(!app || !app->live_active) return 0;

    if(event.event == SerialServiceEventTypeDataReceived) {
        for(uint16_t i = 0; i < event.data.size; i++) {
            const uint8_t byte = event.data.buffer[i];
            if(app->live_rx_count == 0) {
                if(byte == LIVE_FRAME_MAGIC) app->live_rx_frame[app->live_rx_count++] = byte;
                continue;
            }

            app->live_rx_frame[app->live_rx_count++] = byte;
            if(app->live_rx_count != LIVE_FRAME_SIZE) continue;

            uint8_t checksum = 0;
            for(uint8_t j = 1; j < LIVE_FRAME_SIZE - 1U; j++) checksum ^= app->live_rx_frame[j];
            if(checksum == app->live_rx_frame[LIVE_FRAME_SIZE - 1U] &&
               app->live_rx_frame[3] <= SWITCH_HAT_NONE) {
                const SwitchReport report = {
                    .buttons = (uint16_t)app->live_rx_frame[1] |
                               ((uint16_t)app->live_rx_frame[2] << 8),
                    .hat = app->live_rx_frame[3],
                    .lx = app->live_rx_frame[4],
                    .ly = app->live_rx_frame[5],
                    .rx = app->live_rx_frame[6],
                    .ry = app->live_rx_frame[7],
                    .vendor = 0,
                };
                live_queue_report(app, &report);
                live_record_state(app, &report, furi_get_tick());
                app->live_frames_received++;
            } else {
                app->live_frames_dropped++;
            }
            app->live_rx_count = 0;
        }
    } else if(event.event == SerialServiceEventTypesBleResetRequest) {
        app->live_rx_count = 0;
    }

    return BLE_PROFILE_SERIAL_PACKET_SIZE_MAX;
}

static void live_bt_status_callback(BtStatus status, void* context) {
    CivSwitchApp* app = context;
    if(!app) return;
    app->bt_status = status;

    // The built-in BT service installs its RPC callback on every connection.
    // Replace it after each status transition while this FAP owns the profile.
    if(app->live_active && app->ble_profile &&
       (status == BtStatusConnected || status == BtStatusAdvertising)) {
        ble_profile_serial_set_event_callback(
            app->ble_profile,
            BLE_PROFILE_SERIAL_PACKET_SIZE_MAX,
            live_serial_event_callback,
            app);
        ble_profile_serial_set_rpc_active(app->ble_profile, false);
    }
}

static int32_t live_worker(void* context) {
    CivSwitchApp* app = context;
    SwitchReport current = neutral_gamepad();
    uint8_t stale_intervals = 0;

    live_record_state(app, &current, furi_get_tick());
    if(!send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80)) {
        app->live_stop_requested = true;
        return 0;
    }

    while(!app->live_stop_requested) {
        if(app->controller_wake_pending && app->connected) {
            app->controller_wake_pending = false;
            if(!send_controller_wake_sequence(app) ||
               !send_gamepad_report(
                   app, current.buttons, current.hat, current.lx, current.ly, current.rx, current.ry)) {
                app->live_stop_requested = true;
                break;
            }
        }

        SwitchReport report;
        if(furi_message_queue_get(app->live_queue, &report, 50) == FuriStatusOk) {
            current = report;
            stale_intervals = 0;
            if(!send_gamepad_report(
                   app, current.buttons, current.hat, current.lx, current.ly, current.rx, current.ry)) {
                app->live_stop_requested = true;
            } else {
                app->live_reports_sent++;
            }
        } else if(stale_intervals < 5U) {
            stale_intervals++;
            if(stale_intervals == 5U &&
               (current.buttons || current.hat != SWITCH_HAT_NONE || current.lx != 0x80 ||
                current.ly != 0x80 || current.rx != 0x80 || current.ry != 0x80)) {
                current = neutral_gamepad();
                live_record_state(app, &current, furi_get_tick());
                if(!send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80)) {
                    app->live_stop_requested = true;
                }
            }
        }
    }

    current = neutral_gamepad();
    live_record_state(app, &current, furi_get_tick());
    const bool released = send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80);
    if(released) wait_report_complete(app, EP_GAMEPAD_IN);
    return 0;
}

static bool start_live_mode(CivSwitchApp* app) {
    if(!app->connected || !app->usb_dev || app->live_active) return false;

    if(app->live_record_mutex &&
       furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) == FuriStatusOk) {
        const bool recorder_ready = app->live_record_text == NULL;
        if(recorder_ready) {
            app->live_latest_valid = false;
            app->live_recording = false;
            app->live_record_full = false;
            app->live_record_saved = false;
            app->live_record_has_state = false;
            app->live_record_saved_name[0] = '\0';
            app->live_record_elapsed_ms = 0U;
            app->ui_message[0] = '\0';
        }
        furi_mutex_release(app->live_record_mutex);
        if(!recorder_ready) return false;
    }

    app->live_ui_screen = LiveUiMenu;
    app->live_controller_return_screen = LiveUiRecorder;
    app->live_recorder_selection = 0U;

    app->bt = furi_record_open(RECORD_BT);
    if(!app->bt) return false;
    app->live_queue = furi_message_queue_alloc(LIVE_QUEUE_DEPTH, sizeof(SwitchReport));
    if(!app->live_queue) {
        furi_record_close(RECORD_BT);
        app->bt = NULL;
        return false;
    }

    app->live_stop_requested = false;
    app->live_frames_received = 0;
    app->live_reports_sent = 0;
    app->live_frames_dropped = 0;
    app->tx_failure = TxFailureNone;
    app->failed_endpoint = 0;
    app->live_rx_count = 0;
    app->live_active = true;

    bt_disconnect(app->bt);
    furi_delay_ms(200);
    app->ble_profile = bt_profile_start(app->bt, ble_profile_serial, NULL);
    if(!app->ble_profile) {
        app->live_active = false;
        app->live_stop_requested = true;
        furi_message_queue_free(app->live_queue);
        app->live_queue = NULL;
        furi_record_close(RECORD_BT);
        app->bt = NULL;
        return false;
    }

    ble_profile_serial_set_event_callback(
        app->ble_profile,
        BLE_PROFILE_SERIAL_PACKET_SIZE_MAX,
        live_serial_event_callback,
        app);
    ble_profile_serial_set_rpc_active(app->ble_profile, false);
    app->bt_status = BtStatusAdvertising;
    bt_set_status_changed_callback(app->bt, live_bt_status_callback, app);
    furi_hal_bt_start_advertising();

    app->live_worker = furi_thread_alloc();
    furi_thread_set_name(app->live_worker, "SwitchLive");
    furi_thread_set_stack_size(app->live_worker, 2048);
    furi_thread_set_callback(app->live_worker, live_worker);
    furi_thread_set_context(app->live_worker, app);
    furi_thread_start(app->live_worker);
    app->state = RunLive;
    app->live_start_failed = false;
    view_port_update(app->view_port);
    return true;
}

static void stop_live_mode(CivSwitchApp* app) {
    if(!app->live_active) return;
    app->live_stop_requested = true;
    bt_set_status_changed_callback(app->bt, NULL, NULL);
    if(app->ble_profile) {
        ble_profile_serial_set_event_callback(app->ble_profile, 0, NULL, NULL);
        ble_profile_serial_set_rpc_active(app->ble_profile, false);
    }
    if(app->live_worker) {
        furi_thread_join(app->live_worker);
        furi_thread_free(app->live_worker);
        app->live_worker = NULL;
    }
    finish_live_recording(app);
    bt_profile_restore_default(app->bt);
    app->ble_profile = NULL;
    app->live_active = false;
    app->bt_status = BtStatusOff;
    furi_message_queue_free(app->live_queue);
    app->live_queue = NULL;
    furi_record_close(RECORD_BT);
    app->bt = NULL;
}

static char* trim_whitespace(char* text) {
    while(*text && isspace((unsigned char)*text)) text++;
    char* end = text + strlen(text);
    while(end > text && isspace((unsigned char)end[-1])) *--end = '\0';
    return text;
}

static bool name_equals(const char* left, const char* right) {
    while(*left && *right) {
        if(toupper((unsigned char)*left) != toupper((unsigned char)*right)) return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool button_mask_for_name(const char* name, uint16_t* mask) {
    if(name_equals(name, "A")) *mask = SWITCH_BUTTON_A;
    else if(name_equals(name, "B")) *mask = SWITCH_BUTTON_B;
    else if(name_equals(name, "X")) *mask = SWITCH_BUTTON_X;
    else if(name_equals(name, "Y")) *mask = SWITCH_BUTTON_Y;
    else if(name_equals(name, "L")) *mask = SWITCH_BUTTON_L;
    else if(name_equals(name, "R")) *mask = SWITCH_BUTTON_R;
    else if(name_equals(name, "ZL")) *mask = SWITCH_BUTTON_ZL;
    else if(name_equals(name, "ZR")) *mask = SWITCH_BUTTON_ZR;
    else if(name_equals(name, "MINUS")) *mask = SWITCH_BUTTON_MINUS;
    else if(name_equals(name, "PLUS")) *mask = SWITCH_BUTTON_PLUS;
    else if(name_equals(name, "L3")) *mask = SWITCH_BUTTON_L3;
    else if(name_equals(name, "R3")) *mask = SWITCH_BUTTON_R3;
    else if(name_equals(name, "HOME")) *mask = SWITCH_BUTTON_HOME;
    else if(name_equals(name, "CAPTURE")) *mask = SWITCH_BUTTON_CAPTURE;
    else return false;
    return true;
}

static bool hat_for_name(const char* name, uint8_t* hat) {
    if(name_equals(name, "UP")) *hat = 0x00;
    else if(name_equals(name, "UP_RIGHT") || name_equals(name, "UPRIGHT")) *hat = 0x01;
    else if(name_equals(name, "RIGHT")) *hat = 0x02;
    else if(name_equals(name, "DOWN_RIGHT") || name_equals(name, "DOWNRIGHT")) *hat = 0x03;
    else if(name_equals(name, "DOWN")) *hat = 0x04;
    else if(name_equals(name, "DOWN_LEFT") || name_equals(name, "DOWNLEFT")) *hat = 0x05;
    else if(name_equals(name, "LEFT")) *hat = 0x06;
    else if(name_equals(name, "UP_LEFT") || name_equals(name, "UPLEFT")) *hat = 0x07;
    else return false;
    return true;
}

static bool parse_axis(const char* token, MacroStep* step) {
    const char* separator = strchr(token, ':');
    if(!separator || separator == token || !separator[1]) return false;
    char axis[3] = {0};
    const size_t axis_length = (size_t)(separator - token);
    if(axis_length > 2) return false;
    memcpy(axis, token, axis_length);
    char* value_end = NULL;
    const unsigned long value = strtoul(separator + 1, &value_end, 10);
    if(value_end == separator + 1 || *value_end || value > 255) return false;
    if(name_equals(axis, "LX")) step->lx = (uint8_t)value;
    else if(name_equals(axis, "LY")) step->ly = (uint8_t)value;
    else if(name_equals(axis, "RX")) step->rx = (uint8_t)value;
    else if(name_equals(axis, "RY")) step->ry = (uint8_t)value;
    else return false;
    return true;
}

static bool parse_controls(char* controls, MacroStep* step) {
    bool has_hat = false;
    char* part = controls;
    while(part && *part) {
        char* next = strchr(part, '+');
        if(next) {
            *next++ = '\0';
            if(!*next) return false;
        }
        if(!*part) return false;
        uint16_t button_mask = 0;
        uint8_t direction = SWITCH_HAT_NONE;
        if(name_equals(part, "NONE")) {
            if(step->buttons || has_hat) return false;
        } else if(button_mask_for_name(part, &button_mask)) {
            step->buttons |= button_mask;
        } else if(hat_for_name(part, &direction)) {
            if(has_hat) return false;
            step->hat = direction;
            has_hat = true;
        } else if(!parse_axis(part, step)) {
            return false;
        }
        part = next;
    }
    return true;
}

static bool parse_macro_line(char* line, MacroStep* step, bool* has_step) {
    char* comment = strchr(line, '#');
    if(comment) *comment = '\0';
    line = trim_whitespace(line);
    if(!*line) {
        *has_step = false;
        return true;
    }

    char* command = line;
    char* argument = command;
    while(*argument && !isspace((unsigned char)*argument)) argument++;
    if(!*argument) return false;
    *argument++ = '\0';
    argument = trim_whitespace(argument);
    if(!*argument) return false;

    char* duration_text = argument;
    const bool wait_command = name_equals(command, "WAIT");
    if(name_equals(command, "STATE")) {
        char* control_end = duration_text;
        while(*control_end && !isspace((unsigned char)*control_end)) control_end++;
        if(!*control_end) return false;
        *control_end++ = '\0';
        duration_text = trim_whitespace(control_end);
        if(!*duration_text) return false;
        step->state_mode = true;
        step->buttons = 0;
        step->hat = SWITCH_HAT_NONE;
        step->lx = step->ly = step->rx = step->ry = 0x80;
        if(!parse_controls(argument, step)) return false;
    } else {
        char* duration_end = duration_text;
        while(*duration_end && !isspace((unsigned char)*duration_end)) duration_end++;
        if(*duration_end) {
            *duration_end++ = '\0';
            if(*trim_whitespace(duration_end)) return false;
        }
        step->state_mode = false;
        step->buttons = 0;
        step->hat = SWITCH_HAT_NONE;
        step->lx = step->ly = step->rx = step->ry = 0x80;
        step->state_mode = wait_command;
        if(!wait_command && !parse_controls(command, step)) return false;
    }
    char* number_end = NULL;
    const unsigned long duration = strtoul(duration_text, &number_end, 10);
    if(number_end == duration_text || *number_end || duration == 0 || duration > MACRO_MAX_STEP_MS) return false;

    step->duration_ms = (uint32_t)duration;
    if(wait_command) {
        step->state_mode = true;
        step->buttons = 0;
        step->hat = SWITCH_HAT_NONE;
        step->lx = step->ly = step->rx = step->ry = 0x80;
        *has_step = true;
        return true;
    }
    if(!step->state_mode && step->buttons == 0 && step->hat == SWITCH_HAT_NONE &&
       step->lx == 0x80 && step->ly == 0x80 && step->rx == 0x80 && step->ry == 0x80) {
        return false;
    }
    *has_step = true;
    return true;
}

static MacroLoadError parse_macro_text(char* text, MacroStep* steps, uint16_t* step_count, uint16_t* error_line) {
    *step_count = 0;
    uint16_t line_number = 0;
    char* line = text;
    while(*line) {
        line_number++;
        char* next_line = strchr(line, '\n');
        if(next_line) *next_line++ = '\0';
        MacroStep step = {0};
        bool has_step = false;
        if(!parse_macro_line(line, &step, &has_step)) {
            *error_line = line_number;
            return MacroLoadBadSyntax;
        }
        if(has_step) {
            if(*step_count >= MACRO_MAX_STEPS) {
                *error_line = line_number;
                return MacroLoadTooManySteps;
            }
            steps[(*step_count)++] = step;
        }
        if(!next_line) break;
        line = next_line;
    }
    return *step_count ? MacroLoadOk : MacroLoadEmpty;
}

static MacroLoadError load_macro_file(CivSwitchApp* app, const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(!file) {
        furi_record_close(RECORD_STORAGE);
        return MacroLoadOutOfMemory;
    }
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return MacroLoadMissing;
    }

    char* text = malloc(MACRO_FILE_MAX_BYTES + 1U);
    MacroStep* steps = malloc(sizeof(MacroStep) * MACRO_MAX_STEPS);
    if(!text || !steps) {
        free(text);
        free(steps);
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return MacroLoadOutOfMemory;
    }

    size_t total = 0;
    while(total < MACRO_FILE_MAX_BYTES + 1U) {
        const size_t count = storage_file_read(file, (uint8_t*)(text + total), MACRO_FILE_MAX_BYTES + 1U - total);
        if(count == 0) break;
        total += count;
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(total > MACRO_FILE_MAX_BYTES) {
        free(text);
        free(steps);
        return MacroLoadTooLarge;
    }
    if(total == 0) {
        free(text);
        free(steps);
        return MacroLoadEmpty;
    }
    text[total] = '\0';

    uint16_t step_count = 0;
    uint16_t error_line = 0;
    const MacroLoadError result = parse_macro_text(text, steps, &step_count, &error_line);
    free(text);
    if(result != MacroLoadOk) {
        free(steps);
        app->load_error_line = error_line;
        return result;
    }
    app->steps = steps;
    app->step_count = step_count;
    app->load_error_line = 0;
    return MacroLoadOk;
}

static const char* macro_load_error_text(MacroLoadError error) {
    switch(error) {
    case MacroLoadMissing: return "Macro file not found";
    case MacroLoadTooLarge: return "Macro file over 24 KB";
    case MacroLoadEmpty: return "Macro file has no steps";
    case MacroLoadBadSyntax: return "Invalid macro syntax";
    case MacroLoadTooManySteps: return "More than 512 steps";
    case MacroLoadOutOfMemory: return "Not enough memory";
    case MacroLoadOk: return "Macro loaded";
    }
    return "Macro load error";
}

static bool macro_name_has_txt_extension(const char* name) {
    const size_t length = strlen(name);
    if(length < 5U || length >= MACRO_FILENAME_MAX) return false;
    return tolower((unsigned char)name[length - 4U]) == '.' &&
           tolower((unsigned char)name[length - 3U]) == 't' &&
           tolower((unsigned char)name[length - 2U]) == 'x' &&
           tolower((unsigned char)name[length - 1U]) == 't';
}

static int macro_name_compare(const char* left, const char* right) {
    while(*left && *right) {
        const int left_char = tolower((unsigned char)*left++);
        const int right_char = tolower((unsigned char)*right++);
        if(left_char != right_char) return left_char - right_char;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

static void macro_path(char* path, size_t capacity, const char* name) {
    snprintf(path, capacity, "%s/%s", MACRO_DIRECTORY, name);
}

static void refresh_macro_files(CivSwitchApp* app) {
    char previous_name[MACRO_FILENAME_MAX] = {0};
    if(app->macro_count > 0U && app->macro_index < app->macro_count) {
        snprintf(previous_name, sizeof(previous_name), "%s", app->macro_files[app->macro_index].name);
    }
    app->macro_count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Storage unavailable");
        return;
    }
    if(!storage_dir_exists(storage, APP_DATA_DIRECTORY)) {
        storage_common_mkdir(storage, APP_DATA_DIRECTORY);
    }
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) {
        storage_common_mkdir(storage, MACRO_DIRECTORY);
    }

    File* directory = storage_file_alloc(storage);
    if(!directory) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Not enough memory");
        furi_record_close(RECORD_STORAGE);
        return;
    }
    if(!storage_dir_open(directory, MACRO_DIRECTORY)) {
        storage_dir_close(directory);
        storage_file_free(directory);
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "SD folder unavailable");
        return;
    }

    FileInfo info;
    char name[MACRO_FILENAME_MAX];
    while(storage_dir_read(directory, &info, name, sizeof(name))) {
        name[sizeof(name) - 1U] = '\0';
        if((info.flags & FSF_DIRECTORY) || !macro_name_has_txt_extension(name)) continue;
        if(app->macro_count < MACRO_MAX_FILES) {
            snprintf(app->macro_files[app->macro_count].name, MACRO_FILENAME_MAX, "%s", name);
            app->macro_count++;
        } else if(app->live_record_saved && strcmp(name, app->live_record_saved_name) == 0) {
            snprintf(
                app->macro_files[MACRO_MAX_FILES - 1U].name,
                MACRO_FILENAME_MAX,
                "%s",
                name);
        }
    }
    storage_dir_close(directory);
    storage_file_free(directory);
    furi_record_close(RECORD_STORAGE);

    for(uint8_t i = 1U; i < app->macro_count; i++) {
        MacroFile current = app->macro_files[i];
        uint8_t j = i;
        while(j > 0U && macro_name_compare(app->macro_files[j - 1U].name, current.name) > 0) {
            app->macro_files[j] = app->macro_files[j - 1U];
            j--;
        }
        app->macro_files[j] = current;
    }

    app->macro_index = 0;
    for(uint8_t i = 0U; i < app->macro_count; i++) {
        if(strcmp(app->macro_files[i].name, previous_name) == 0) {
            app->macro_index = i;
            break;
        }
        if(strcmp(app->macro_files[i].name, MACRO_DEFAULT_NAME) == 0) app->macro_index = i;
    }
    normalize_macro_list_offset(app);
    app->ui_message[0] = '\0';
}

static bool start_live_recording(CivSwitchApp* app) {
    if(!app->live_record_mutex) return false;
    char* text = malloc(MACRO_FILE_MAX_BYTES + 1U);
    if(!text) return false;
    const size_t header_length = sizeof(LIVE_RECORD_HEADER) - 1U;
    memcpy(text, LIVE_RECORD_HEADER, header_length + 1U);

    if(furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) != FuriStatusOk) {
        free(text);
        return false;
    }
    if(app->live_record_text) {
        furi_mutex_release(app->live_record_mutex);
        free(text);
        return false;
    }
    app->live_record_text = text;
    app->live_record_bytes = (uint16_t)header_length;
    app->live_record_steps = 0U;
    app->live_record_full = false;
    app->live_record_saved = false;
    app->live_record_saved_name[0] = '\0';
    app->live_record_started_tick = furi_get_tick();
    app->live_record_elapsed_ms = 0U;
    app->live_recording = true;
    app->ui_message[0] = '\0';
    app->live_record_has_state = app->live_latest_valid;
    if(app->live_latest_valid) {
        app->live_record_state = app->live_latest_state;
        app->live_record_last_tick = furi_get_tick();
    }
    furi_mutex_release(app->live_record_mutex);
    return true;
}

static bool finish_live_recording(CivSwitchApp* app) {
    if(!app->live_record_mutex || furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) != FuriStatusOk) {
        return false;
    }
    char* text = app->live_record_text;
    if(!text) {
        furi_mutex_release(app->live_record_mutex);
        return false;
    }
    if(app->live_recording) {
        app->live_record_elapsed_ms = furi_get_tick() - app->live_record_started_tick;
    }
    if(app->live_recording && app->live_record_has_state) {
        live_record_append_step_locked(
            app,
            &app->live_record_state,
            furi_get_tick() - app->live_record_last_tick);
    }
    app->live_recording = false;
    app->live_record_full = false;
    app->live_record_has_state = false;
    app->live_record_text = NULL;
    const uint16_t byte_count = app->live_record_bytes;
    const uint16_t step_count = app->live_record_steps;
    app->live_record_bytes = 0U;
    furi_mutex_release(app->live_record_mutex);

    if(step_count == 0U) {
        free(text);
        snprintf(app->ui_message, sizeof(app->ui_message), "No input to save");
        return false;
    }

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        free(text);
        snprintf(app->ui_message, sizeof(app->ui_message), "SD unavailable");
        return false;
    }
    if(!storage_dir_exists(storage, APP_DATA_DIRECTORY)) storage_common_mkdir(storage, APP_DATA_DIRECTORY);
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) storage_common_mkdir(storage, MACRO_DIRECTORY);

    bool saved = false;
    char saved_name[MACRO_FILENAME_MAX] = {0};
    char path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    for(uint16_t index = 1U; index <= 9999U; index++) {
        snprintf(saved_name, sizeof(saved_name), "live_%04u.txt", (unsigned)index);
        macro_path(path, sizeof(path), saved_name);
        if(storage_common_exists(storage, path)) continue;

        File* file = storage_file_alloc(storage);
        if(!file) break;
        const bool opened = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
        if(!opened) {
            storage_file_close(file);
            storage_file_free(file);
            if(storage_common_exists(storage, path)) continue;
            break;
        }
        const size_t written = storage_file_write(file, text, byte_count);
        const bool synced = storage_file_sync(file);
        const bool closed = storage_file_close(file);
        storage_file_free(file);
        if(written == byte_count && synced && closed) {
            saved = true;
        } else {
            storage_common_remove(storage, path);
        }
        break;
    }
    furi_record_close(RECORD_STORAGE);
    free(text);

    if(saved) {
        app->live_record_saved = true;
        snprintf(app->live_record_saved_name, sizeof(app->live_record_saved_name), "%s", saved_name);
        snprintf(app->ui_message, sizeof(app->ui_message), "Saved %.24s", saved_name);
    } else {
        app->live_record_saved = false;
        snprintf(app->ui_message, sizeof(app->ui_message), "SD write failed");
    }
    return saved;
}

static void load_selected_macro(CivSwitchApp* app) {
    if(app->steps) {
        free(app->steps);
        app->steps = NULL;
    }
    app->step_count = 0;
    app->macro_total_ms = 0U;
    app->load_error_line = 0;
    if(app->macro_count == 0U || app->macro_index >= app->macro_count) {
        app->load_error = MacroLoadMissing;
        return;
    }
    char path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    macro_path(path, sizeof(path), app->macro_files[app->macro_index].name);
    app->load_error = load_macro_file(app, path);
    if(app->load_error != MacroLoadOk) {
        snprintf(app->ui_message, sizeof(app->ui_message), "%s", macro_load_error_text(app->load_error));
    } else {
        for(uint16_t i = 0U; i < app->step_count; i++) {
            app->macro_total_ms += app->steps[i].duration_ms;
        }
        app->ui_message[0] = '\0';
    }
}

static void select_macro_named(CivSwitchApp* app, const char* name) {
    for(uint8_t i = 0U; i < app->macro_count; i++) {
        if(strcmp(app->macro_files[i].name, name) == 0) {
            app->macro_index = i;
            normalize_macro_list_offset(app);
            return;
        }
    }
}

static bool delete_selected_macro(CivSwitchApp* app) {
    if(app->macro_count == 0U || app->macro_index >= app->macro_count) return false;
    char path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    macro_path(path, sizeof(path), app->macro_files[app->macro_index].name);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool success = storage_common_remove(storage, path) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(!success) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Delete failed");
        return false;
    }
    snprintf(app->ui_message, sizeof(app->ui_message), "Macro deleted");
    refresh_macro_files(app);
    load_selected_macro(app);
    snprintf(app->ui_message, sizeof(app->ui_message), "%s", app->macro_count ? "Macro deleted" : "No macros left");
    return true;
}

static bool rename_selected_macro(CivSwitchApp* app) {
    const size_t base_length = strlen(app->rename_name);
    if(app->macro_count == 0U || app->macro_index >= app->macro_count || base_length == 0U) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Name cannot be empty");
        return false;
    }
    char new_name[MACRO_FILENAME_MAX];
    if(snprintf(new_name, sizeof(new_name), "%s.txt", app->rename_name) >= (int)sizeof(new_name)) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Name is too long");
        return false;
    }
    char old_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    char new_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    macro_path(old_path, sizeof(old_path), app->macro_files[app->macro_index].name);
    macro_path(new_path, sizeof(new_path), new_name);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(storage_common_exists(storage, new_path) && !storage_common_equivalent_path(storage, old_path, new_path)) {
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Name already exists");
        return false;
    }
    const bool success = storage_common_rename(storage, old_path, new_path) == FSE_OK;
    furi_record_close(RECORD_STORAGE);
    if(!success) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Rename failed");
        return false;
    }
    refresh_macro_files(app);
    select_macro_named(app, new_name);
    load_selected_macro(app);
    snprintf(app->ui_message, sizeof(app->ui_message), "Macro renamed");
    return true;
}

static int32_t macro_worker(void* ctx);

static void begin_macro_rename(CivSwitchApp* app) {
    const char* filename = app->macro_files[app->macro_index].name;
    const size_t filename_length = strlen(filename);
    const size_t base_length = filename_length - 4U;
    if(base_length > MACRO_BASENAME_MAX) return;
    memcpy(app->rename_name, filename, base_length);
    app->rename_name[base_length] = '\0';
    app->rename_position = base_length;
    app->rename_keyboard_row = 0U;
    app->rename_keyboard_column = 0U;
    app->rename_symbols = false;
    app->rename_cursor_selected = false;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiRename;
}

static uint8_t nearest_rename_column(const CivSwitchApp* app, uint8_t row, uint8_t x) {
    uint8_t key_count;
    const RenameKey* keys = get_rename_keyboard_row(app, row, &key_count);
    uint8_t best_column = 0U;
    uint8_t best_distance = UINT8_MAX;
    for(uint8_t column = 0U; column < key_count; column++) {
        const uint8_t distance = keys[column].x > x ? keys[column].x - x : x - keys[column].x;
        if(distance < best_distance) {
            best_distance = distance;
            best_column = column;
        }
    }
    return best_column;
}

static void move_rename_vertical(CivSwitchApp* app, bool up) {
    if(app->rename_cursor_selected) {
        if(!up) app->rename_cursor_selected = false;
        return;
    }

    if(up && app->rename_keyboard_row == 0U) {
        app->rename_cursor_selected = true;
        return;
    }
    if(!up && app->rename_keyboard_row == 2U) return;

    uint8_t key_count;
    const RenameKey* keys = get_rename_keyboard_row(
        app, app->rename_keyboard_row, &key_count);
    UNUSED(key_count);
    const uint8_t target_x = keys[app->rename_keyboard_column].x;
    app->rename_keyboard_row = up ? app->rename_keyboard_row - 1U : app->rename_keyboard_row + 1U;
    app->rename_keyboard_column = nearest_rename_column(app, app->rename_keyboard_row, target_x);
}

static void move_rename_horizontal(CivSwitchApp* app, bool right) {
    if(app->rename_cursor_selected) {
        const size_t length = strlen(app->rename_name);
        if(right && app->rename_position < length) app->rename_position++;
        else if(!right && app->rename_position > 0U) app->rename_position--;
        return;
    }

    uint8_t key_count;
    (void)get_rename_keyboard_row(app, app->rename_keyboard_row, &key_count);
    if(right) {
        app->rename_keyboard_column = (app->rename_keyboard_column + 1U) % key_count;
    } else {
        app->rename_keyboard_column = (app->rename_keyboard_column + key_count - 1U) % key_count;
    }
}

static void activate_rename_key(CivSwitchApp* app) {
    if(app->rename_cursor_selected) {
        app->rename_cursor_selected = false;
        return;
    }

    uint8_t key_count;
    const RenameKey* keys = get_rename_keyboard_row(app, app->rename_keyboard_row, &key_count);
    if(app->rename_keyboard_column >= key_count) return;
    const RenameKey* key = &keys[app->rename_keyboard_column];
    if(key->action == RenameKeySymbols) {
        const uint8_t previous_x = key->x;
        app->rename_symbols = !app->rename_symbols;
        app->rename_keyboard_column = nearest_rename_column(app, app->rename_keyboard_row, previous_x);
    } else if(key->action == RenameKeySave) {
        if(rename_selected_macro(app)) app->macro_ui_screen = MacroUiList;
    } else if(key->action == RenameKeyBackspace) {
        if(app->rename_position > 0U) {
            const size_t position = app->rename_position;
            memmove(
                app->rename_name + position - 1U,
                app->rename_name + position,
                strlen(app->rename_name + position) + 1U);
            app->rename_position--;
            app->ui_message[0] = '\0';
        }
    } else {
        const size_t length = strlen(app->rename_name);
        if(length >= MACRO_BASENAME_MAX) {
            snprintf(app->ui_message, sizeof(app->ui_message), "Name is too long");
            return;
        }
        const char value = key->action == RenameKeySpace ? ' ' : key->value;
        const size_t position = app->rename_position > length ? length : app->rename_position;
        memmove(app->rename_name + position + 1U, app->rename_name + position, length - position + 1U);
        app->rename_name[position] = value;
        app->rename_position = position + 1U;
        app->ui_message[0] = '\0';
    }
}

static void start_macro_playback(CivSwitchApp* app) {
    if(app->load_error != MacroLoadOk || app->worker) return;
    if(!app->connected) {
        snprintf(app->ui_message, sizeof(app->ui_message), "USB controller not ready");
        return;
    }
    app->stop_requested = false;
    app->completed_steps = 0U;
    app->macro_elapsed_ms = 0U;
    app->macro_cycles = 0U;
    app->state = RunSending;
    app->worker = furi_thread_alloc();
    if(!app->worker) {
        app->state = RunReady;
        snprintf(app->ui_message, sizeof(app->ui_message), "Not enough memory");
        return;
    }
    furi_thread_set_name(app->worker, "SwitchMacro");
    furi_thread_set_stack_size(app->worker, 2048);
    furi_thread_set_callback(app->worker, macro_worker);
    furi_thread_set_context(app->worker, app);
    furi_thread_start(app->worker);
}

static uint8_t simulator_move_selection(uint8_t current, InputKey key) {
    if(current >= SimulatorControlCount) return SimulatorControlA;

    int16_t direction_x = 0;
    int16_t direction_y = 0;
    if(key == InputKeyUp) direction_y = -1;
    else if(key == InputKeyDown) direction_y = 1;
    else if(key == InputKeyLeft) direction_x = -1;
    else if(key == InputKeyRight) direction_x = 1;
    else return current;

    const SimulatorControl* origin = &simulator_controls[current];
    uint8_t best = current;
    uint16_t best_score = 0xFFFFU;
    for(uint8_t i = 0U; i < SimulatorControlCount; i++) {
        if(i == current) continue;
        const int16_t delta_x = (int16_t)simulator_controls[i].x - origin->x;
        const int16_t delta_y = (int16_t)simulator_controls[i].y - origin->y;
        const int16_t forward = delta_x * direction_x + delta_y * direction_y;
        if(forward <= 0) continue;

        const int16_t lateral = direction_x ? delta_y : delta_x;
        const uint16_t score = (uint16_t)(forward + (lateral < 0 ? -lateral : lateral) * 2);
        if(score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

static bool simulator_selection_is_stick(uint8_t selection) {
    return selection == SimulatorControlLeftStick || selection == SimulatorControlRightStick;
}

static bool simulator_adjust_stick(CivSwitchApp* app, InputKey key) {
    if(!app->connected) {
        app->simulator_error = true;
        return false;
    }

    SwitchReport next = app->simulator_report;
    uint8_t* horizontal = app->simulator_selection == SimulatorControlLeftStick ? &next.lx : &next.rx;
    uint8_t* vertical = app->simulator_selection == SimulatorControlLeftStick ? &next.ly : &next.ry;
    uint8_t* axis = NULL;
    int16_t delta = 0;
    if(key == InputKeyUp) {
        axis = vertical;
        delta = -(int16_t)SIMULATOR_STICK_STEP;
    } else if(key == InputKeyDown) {
        axis = vertical;
        delta = SIMULATOR_STICK_STEP;
    } else if(key == InputKeyLeft) {
        axis = horizontal;
        delta = -(int16_t)SIMULATOR_STICK_STEP;
    } else if(key == InputKeyRight) {
        axis = horizontal;
        delta = SIMULATOR_STICK_STEP;
    } else {
        return false;
    }

    int16_t value = (int16_t)*axis + delta;
    if(value < 0) value = 0;
    if(value > 255) value = 255;
    *axis = (uint8_t)value;
    if(send_gamepad_report(app, next.buttons, next.hat, next.lx, next.ly, next.rx, next.ry)) {
        app->simulator_report = next;
        app->simulator_error = false;
        return true;
    }
    app->simulator_error = true;
    return false;
}

static void handle_simulator_input(CivSwitchApp* app, InputKey key, bool repeat) {
    if(key == InputKeyUp || key == InputKeyDown || key == InputKeyLeft || key == InputKeyRight) {
        if(app->simulator_stick_active && simulator_selection_is_stick(app->simulator_selection)) {
            simulator_adjust_stick(app, key);
        } else if(!repeat) {
            app->simulator_selection = simulator_move_selection(app->simulator_selection, key);
        }
    } else if(key == InputKeyOk) {
        if(repeat) return;
        if(simulator_selection_is_stick(app->simulator_selection)) {
            app->simulator_stick_active = !app->simulator_stick_active;
            return;
        }
        if(!app->connected) {
            app->simulator_error = true;
            return;
        }

        const SimulatorControl* control = &simulator_controls[app->simulator_selection];
        SwitchReport next = app->simulator_report;
        if(control->button) {
            next.buttons |= control->button;
        } else {
            next.hat = control->hat;
        }

        if(send_gamepad_report(app, next.buttons, next.hat, next.lx, next.ly, next.rx, next.ry)) {
            app->simulator_report = next;
            app->simulator_held_control = app->simulator_selection;
            app->simulator_error = false;
        } else {
            app->simulator_error = true;
        }
    } else if(key == InputKeyBack) {
        if(app->connected &&
           send_gamepad_report(app, 0U, SWITCH_HAT_NONE, 0x80U, 0x80U, 0x80U, 0x80U)) {
            wait_report_complete(app, EP_GAMEPAD_IN);
        }
        app->simulator_report = neutral_gamepad();
        app->simulator_held_control = SIMULATOR_NO_CONTROL;
        app->simulator_stick_active = false;
        app->simulator_error = false;
        app->macro_ui_screen = MacroUiHome;
    }
}

static void release_simulator_button(CivSwitchApp* app, InputKey key) {
    if(key != InputKeyOk || app->simulator_held_control >= SimulatorControlCount) return;

    const SimulatorControl* control = &simulator_controls[app->simulator_held_control];
    SwitchReport next = app->simulator_report;
    if(control->button) {
        next.buttons &= (uint16_t)~control->button;
    } else if(next.hat == control->hat) {
        next.hat = SWITCH_HAT_NONE;
    }
    app->simulator_held_control = SIMULATOR_NO_CONTROL;

    if(app->connected &&
       send_gamepad_report(app, next.buttons, next.hat, next.lx, next.ly, next.rx, next.ry)) {
        app->simulator_report = next;
        app->simulator_error = false;
    } else {
        app->simulator_error = true;
    }
}

static void handle_macro_ui_input(CivSwitchApp* app, InputKey key, bool* running) {
    if(app->macro_ui_screen == MacroUiHome) {
        if(key == InputKeyUp) {
            app->home_index = (app->home_index + 2U) % 3U;
        } else if(key == InputKeyDown) {
            app->home_index = (app->home_index + 1U) % 3U;
        } else if(key == InputKeyOk && app->home_index == 0U) {
            app->live_start_failed = false;
            refresh_macro_files(app);
            if(app->live_record_saved) select_macro_named(app, app->live_record_saved_name);
            load_selected_macro(app);
            app->macro_ui_screen = MacroUiList;
        } else if(key == InputKeyOk && app->home_index == 1U) {
            app->live_start_failed = !start_live_mode(app);
        } else if(key == InputKeyOk) {
            if(app->connected) {
                app->simulator_report = neutral_gamepad();
                app->simulator_selection = SimulatorControlA;
                app->simulator_held_control = SIMULATOR_NO_CONTROL;
                app->simulator_error = false;
                app->simulator_stick_active = false;
                app->ui_message[0] = '\0';
                app->macro_ui_screen = MacroUiSimulator;
            } else {
                snprintf(app->ui_message, sizeof(app->ui_message), "USB controller not ready");
            }
        } else if(key == InputKeyBack) {
            *running = false;
        }
    } else if(app->macro_ui_screen == MacroUiList) {
        if((key == InputKeyUp || key == InputKeyDown) && app->macro_count > 0U) {
            if(key == InputKeyUp) {
                app->macro_index = (app->macro_index + app->macro_count - 1U) % app->macro_count;
            } else {
                app->macro_index = (app->macro_index + 1U) % app->macro_count;
            }
            normalize_macro_list_offset(app);
            app->macro_loop = false;
            app->macro_detail_selection = 1U;
            load_selected_macro(app);
        } else if(key == InputKeyOk) {
            if(app->load_error == MacroLoadOk) {
                app->macro_detail_selection = 1U;
                app->macro_ui_screen = MacroUiDetails;
            }
        } else if(key == InputKeyRight && app->macro_count > 0U) {
            app->macro_action_index = 0U;
            app->macro_ui_screen = MacroUiActions;
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiHome;
        }
    } else if(app->macro_ui_screen == MacroUiDetails) {
        if(key == InputKeyUp || key == InputKeyDown) {
            app->macro_detail_selection = (app->macro_detail_selection + 1U) % 2U;
        } else if(key == InputKeyOk) {
            if(app->macro_detail_selection == 0U) {
                app->macro_loop = !app->macro_loop;
            } else {
                start_macro_playback(app);
            }
        } else if(key == InputKeyRight) {
            app->macro_action_index = 0U;
            app->macro_ui_screen = MacroUiActions;
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiList;
        }
    } else if(app->macro_ui_screen == MacroUiActions) {
        if(key == InputKeyUp) {
            app->macro_action_index = (app->macro_action_index + 2U) % 3U;
        } else if(key == InputKeyDown) {
            app->macro_action_index = (app->macro_action_index + 1U) % 3U;
        } else if(key == InputKeyBack || key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiList;
        } else if(key == InputKeyOk) {
            if(app->macro_action_index == 0U) {
                app->macro_ui_screen = MacroUiList;
                start_macro_playback(app);
            } else if(app->macro_action_index == 1U) {
                begin_macro_rename(app);
            } else {
                app->macro_ui_screen = MacroUiDeleteConfirm;
            }
        }
    } else if(app->macro_ui_screen == MacroUiRename) {
        if(key == InputKeyUp || key == InputKeyDown) {
            move_rename_vertical(app, key == InputKeyUp);
        } else if(key == InputKeyLeft) {
            move_rename_horizontal(app, false);
        } else if(key == InputKeyRight) {
            move_rename_horizontal(app, true);
        } else if(key == InputKeyOk) {
            activate_rename_key(app);
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiActions;
        }
    } else if(app->macro_ui_screen == MacroUiDeleteConfirm) {
        if(key == InputKeyOk) {
            app->macro_ui_screen = MacroUiList;
            delete_selected_macro(app);
        } else if(key == InputKeyBack || key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiActions;
        }
    } else if(app->macro_ui_screen == MacroUiSimulator) {
        handle_simulator_input(app, key, false);
    }
}

static void handle_input(CivSwitchApp* app, InputKey key, bool* running) {
    if(app->state == RunReady) {
        handle_macro_ui_input(app, key, running);
    } else if(app->state == RunLive) {
        if(app->live_ui_screen == LiveUiMenu) {
            if(key == InputKeyOk) {
                app->live_recorder_selection = 0U;
                app->live_ui_screen = LiveUiRecorder;
            } else if(key == InputKeyBack) {
                stop_live_mode(app);
                app->state = RunReady;
            }
        } else if(app->live_ui_screen == LiveUiRecorder) {
            if(key == InputKeyUp || key == InputKeyDown) {
                app->live_recorder_selection = (uint8_t)(1U - app->live_recorder_selection);
            } else if(key == InputKeyOk) {
                if(app->live_recorder_selection == 1U) {
                    app->live_controller_return_screen = LiveUiRecorder;
                    app->live_ui_screen = LiveUiController;
                } else if(app->live_recording || app->live_record_full) {
                    finish_live_recording(app);
                } else if(!start_live_recording(app)) {
                    snprintf(app->ui_message, sizeof(app->ui_message), "Record unavailable");
                }
            } else if(key == InputKeyBack) {
                app->live_ui_screen = LiveUiMenu;
            }
        } else if(key == InputKeyBack) {
            app->live_ui_screen = app->live_controller_return_screen;
        }
    } else if(key == InputKeyBack) {
        if(app->state == RunSending) {
            app->stop_requested = true;
        } else {
            app->state = RunReady;
            app->macro_ui_screen = MacroUiList;
            refresh_macro_files(app);
            load_selected_macro(app);
            if(app->worker) {
                furi_thread_join(app->worker);
                furi_thread_free(app->worker);
                app->worker = NULL;
            }
        }
    }
}

static int32_t macro_worker(void* ctx) {
    CivSwitchApp* app = ctx;
    app->state = RunSending;
    app->failed_endpoint = 0;
    app->tx_failure = TxFailureNone;
    app->usb_result = 0;

    bool failed = false;
    do {
        app->completed_steps = 0U;
        app->macro_elapsed_ms = 0U;
        for(uint16_t i = 0; i < app->step_count && !app->stop_requested; i++) {
            const MacroStep* step = &app->steps[i];
            if(!send_gamepad_report(
                   app, step->buttons, step->hat, step->lx, step->ly, step->rx, step->ry)) {
                failed = true;
                break;
            }
            if(!delay_or_cancel(app, step->duration_ms)) break;
            if(!step->state_mode && !send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80)) {
                failed = true;
                break;
            }
            app->completed_steps = i + 1U;
            view_port_update(app->view_port);
        }
        if(failed || app->stop_requested) break;
        app->macro_cycles++;
    } while(app->macro_loop);

    if(app->connected && app->usb_dev) {
        const bool pad_queued = send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80);
        const bool pad_released = pad_queued && wait_report_complete(app, EP_GAMEPAD_IN);
        KeyboardReport keyboard = {0};
        const bool keyboard_sent = send_report(app, EP_KEYBOARD_IN, &keyboard, sizeof(keyboard));
        const bool keyboard_released = keyboard_sent && wait_report_complete(app, EP_KEYBOARD_IN);
        if(!pad_released || !keyboard_released) failed = true;
    }

    if(app->tx_failure != TxFailureNone || failed) app->state = RunError;
    else if(app->stop_requested) app->state = RunCanceled;
    else app->state = RunFinished;
    view_port_update(app->view_port);
    return 0;
}

int32_t civswitch_app(void* p) {
    UNUSED(p);
    CivSwitchApp app = {0};
    active_app = &app;
    app.state = RunReady;
    app.macro_ui_screen = MacroUiHome;
    app.live_record_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.previous_usb = furi_hal_usb_get_config();
    refresh_macro_files(&app);
    load_selected_macro(&app);
    app.input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app.view_port = view_port_alloc();
    app.gui = furi_record_open(RECORD_GUI);
    view_port_draw_callback_set(app.view_port, app_draw, &app);
    view_port_input_callback_set(app.view_port, app_input, &app);
    gui_add_view_port(app.gui, app.view_port, GuiLayerFullscreen);
    furi_hal_usb_set_state_callback(usb_state_callback, &app);
    furi_check(furi_hal_usb_set_config(&civswitch_usb, NULL));

    bool running = true;
    while(running) {
        service_controller_wake(&app);
        InputEvent event;
        if(furi_message_queue_get(app.input_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                handle_input(&app, event.key, &running);
            } else if(app.state == RunReady && app.macro_ui_screen == MacroUiSimulator) {
                if(event.type == InputTypeRelease) {
                    release_simulator_button(&app, event.key);
                } else if(event.type == InputTypeRepeat && app.simulator_stick_active &&
                          simulator_selection_is_stick(app.simulator_selection)) {
                    handle_simulator_input(&app, event.key, true);
                }
            }
        }
        view_port_update(app.view_port);
    }

    if(app.worker) {
        app.stop_requested = true;
        furi_thread_join(app.worker);
        furi_thread_free(app.worker);
    }
    stop_live_mode(&app);
    finish_live_recording(&app);
    if(app.steps) free(app.steps);
    gui_remove_view_port(app.gui, app.view_port);
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app.input_queue);
    furi_hal_usb_set_state_callback(NULL, NULL);
    furi_check(furi_hal_usb_set_config(app.previous_usb, NULL));
    if(app.gamepad_tx_sem) furi_semaphore_free(app.gamepad_tx_sem);
    if(app.keyboard_tx_sem) furi_semaphore_free(app.keyboard_tx_sem);
    if(app.live_record_mutex) furi_mutex_free(app.live_record_mutex);
    active_app = NULL;
    return 0;
}
