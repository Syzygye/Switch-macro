#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>
#include <furi_hal_usb.h>
#include <usb.h>
#include <usb_hid.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>
#include <profiles/serial_profile.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TAG "SwitchMacro"

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
#define BACKUP_DIRECTORY "/ext/apps_data/switch_macro/backups"
#define APP_DATA_DIRECTORY "/ext/apps_data"
#define SETTINGS_PATH "/ext/apps_data/switch_macro/settings.bin"
#define ERROR_LOG_PATH "/ext/apps_data/switch_macro/errors.log"
#define ERROR_LOG_MAX_BYTES 2048U
#define MACRO_DEFAULT_NAME "macros.txt"
#define MACRO_FILENAME_MAX 32U
#define MACRO_BASENAME_MAX 27U
#define MACRO_MAX_FILES 24U
#define BACKUP_MAX_FILES 10U
#define BACKUP_LIST_VISIBLE 3U
#define BACKUP_ACTION_VISIBLE 4U
#define MACRO_LIST_VISIBLE 4
#define INFO_VISIBLE_ROWS 3U
#define INFO_LINE_COUNT 6U
#define INFO_SCROLLBAR_HEIGHT 27U
#define INFO_MASCOT_FRAME_MS 1400U
#define INFO_MASCOT_STEP_MS 1000U
#define LIVE_RECORD_INITIAL_CAPACITY 128U
#define RECORDING_LIMIT_COUNT 4U
#define MACRO_FILE_MAX_BYTES 49152U
#define MACRO_MAX_STEPS 1024U
#define MACRO_MAX_STEP_MS 60000U
#define MACRO_RECORD_AXIS_EPSILON 8U
#define LIVE_QUEUE_DEPTH 1U
#define UI_INPUT_QUEUE_DEPTH 32U
#define LIVE_FRAME_MAGIC 0xC7U
#define LIVE_FRAME_SIZE 9U
#define LIVE_RECENT_INPUT_COUNT 3U
#define SIMULATOR_NO_CONTROL 0xFFU
#define PROFILE_COUNT 3U
#define PROFILE_NAME_MAX 9U
#define MACRO_PREF_COUNT MACRO_MAX_FILES
#define MACRO_NO_PROFILE 0xFFU
#define ERROR_BLINK_HALF_PERIOD_MS 250U
#define ERROR_BLINK_DURATION_MS (ERROR_BLINK_HALF_PERIOD_MS * 4U)
#define SWITCH_MACRO_SETTINGS_MAGIC 0x53574D43UL
#define SWITCH_MACRO_VERSION "0.9.14"
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
    MacroUiOptions,
    MacroUiInfo,
    MacroUiExitConfirm,
    MacroUiPlay,
    MacroUiViewController,
    MacroUiActions,
    MacroUiRename,
    MacroUiDeleteConfirm,
    MacroUiPreview,
    MacroUiEditor,
    MacroUiAssignProfile,
    MacroUiSimulator,
    MacroUiProfiles,
    MacroUiCalibration,
    MacroUiBackupList,
    MacroUiBackupDetails,
    MacroUiBackupPreview,
    MacroUiBackupConflict,
    MacroUiBackupDeleteConfirm,
} MacroUiScreen;

typedef enum {
    MacroListAll,
    MacroListFavorites,
    MacroListRecent,
} MacroListMode;

typedef enum {
    RenameTargetMacro,
    RenameTargetProfile,
    RenameTargetRecording,
    RenameTargetRestore,
} RenameTarget;

typedef enum {
    LiveUiRecorder,
    LiveUiController,
    LiveUiRenameRecording,
} LiveUiScreen;

typedef enum {
    RenameKeyCharacter,
    RenameKeyBackspace,
    RenameKeySymbols,
    RenameKeyCase,
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

typedef struct {
    char filename[MACRO_FILENAME_MAX];
    char title[MACRO_BASENAME_MAX + 1U];
    char date_time[20];
    uint32_t timestamp;
    uint32_t duration_ms;
    uint16_t step_count;
} MacroBackup;

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
    [SimulatorControlL] = {"L", SWITCH_BUTTON_L, SWITCH_HAT_NONE, 27U, 9U},
    [SimulatorControlZL] = {"ZL", SWITCH_BUTTON_ZL, SWITCH_HAT_NONE, 44U, 9U},
    [SimulatorControlHome] = {"HOME", SWITCH_BUTTON_HOME, SWITCH_HAT_NONE, 64U, 15U},
    [SimulatorControlZR] = {"ZR", SWITCH_BUTTON_ZR, SWITCH_HAT_NONE, 84U, 9U},
    [SimulatorControlR] = {"R", SWITCH_BUTTON_R, SWITCH_HAT_NONE, 100U, 13U},
    [SimulatorControlLeftStick] = {"L STICK", 0U, SWITCH_HAT_NONE, 27U, 23U},
    [SimulatorControlL3] = {"L3", SWITCH_BUTTON_L3, SWITCH_HAT_NONE, 17U, 39U},
    [SimulatorControlDpadUp] = {"UP", 0U, 0U, 43U, 30U},
    [SimulatorControlDpadLeft] = {"LEFT", 0U, 6U, 35U, 37U},
    [SimulatorControlDpadDown] = {"DOWN", 0U, 4U, 43U, 45U},
    [SimulatorControlDpadRight] = {"RIGHT", 0U, 2U, 51U, 37U},
    [SimulatorControlMinus] = {"MINUS", SWITCH_BUTTON_MINUS, SWITCH_HAT_NONE, 57U, 23U},
    [SimulatorControlCapture] = {"CAPTURE", SWITCH_BUTTON_CAPTURE, SWITCH_HAT_NONE, 64U, 30U},
    [SimulatorControlPlus] = {"PLUS", SWITCH_BUTTON_PLUS, SWITCH_HAT_NONE, 72U, 23U},
    [SimulatorControlRightStick] = {"R STICK", 0U, SWITCH_HAT_NONE, 82U, 37U},
    [SimulatorControlR3] = {"R3", SWITCH_BUTTON_R3, SWITCH_HAT_NONE, 82U, 56U},
    [SimulatorControlX] = {"X", SWITCH_BUTTON_X, SWITCH_HAT_NONE, 109U, 22U},
    [SimulatorControlY] = {"Y", SWITCH_BUTTON_Y, SWITCH_HAT_NONE, 99U, 33U},
    [SimulatorControlA] = {"A", SWITCH_BUTTON_A, SWITCH_HAT_NONE, 119U, 33U},
    [SimulatorControlB] = {"B", SWITCH_BUTTON_B, SWITCH_HAT_NONE, 109U, 44U},
};

static const uint8_t profile_deadzones[] = {0U, 4U, 8U, 12U, 16U, 20U};
static const uint8_t profile_gains[] = {100U, 110U, 120U, 130U, 140U, 150U};
static const uint8_t profile_simulator_steps[] = {24U, 32U, 48U, 64U, 96U, 128U};
static const uint8_t playback_speeds[] = {50U, 75U, 100U, 125U, 150U, 200U};
static const uint8_t macro_repeat_choices[] = {1U, 2U, 3U, 5U, 10U};
static const uint8_t recording_limit_minutes[RECORDING_LIMIT_COUNT] = {10U, 30U, 45U, 60U};
static const char* const default_profile_names[PROFILE_COUNT] = {"GAME 1", "GAME 2", "GAME 3"};
static const struct {
    const char* name;
    uint16_t mask;
} macro_edit_buttons[] = {
    {"A", SWITCH_BUTTON_A}, {"B", SWITCH_BUTTON_B}, {"X", SWITCH_BUTTON_X},
    {"Y", SWITCH_BUTTON_Y}, {"L", SWITCH_BUTTON_L}, {"R", SWITCH_BUTTON_R},
    {"ZL", SWITCH_BUTTON_ZL}, {"ZR", SWITCH_BUTTON_ZR}, {"MINUS", SWITCH_BUTTON_MINUS},
    {"PLUS", SWITCH_BUTTON_PLUS}, {"L3", SWITCH_BUTTON_L3}, {"R3", SWITCH_BUTTON_R3},
    {"HOME", SWITCH_BUTTON_HOME}, {"CAPTURE", SWITCH_BUTTON_CAPTURE},
};
static const uint8_t macro_edit_hats[] = {
    SWITCH_HAT_NONE, 0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
};

typedef enum {
    MacroLoadOk,
    MacroLoadMissing,
    MacroLoadTooLarge,
    MacroLoadEmpty,
    MacroLoadBadSyntax,
    MacroLoadTooManySteps,
    MacroLoadOutOfMemory,
    MacroLoadStorageUnavailable,
    MacroLoadReadFailed,
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
    uint16_t buttons;
    uint8_t hat;
    uint32_t tick;
} LiveRecentInput;

typedef struct {
    uint8_t left_deadzone;
    uint8_t right_deadzone;
    uint8_t left_gain_percent;
    uint8_t right_gain_percent;
    uint8_t simulator_step;
    uint8_t playback_speed_index;
} GameProfile;

typedef struct {
    uint8_t deadzone;
    uint8_t gain_percent;
    uint8_t simulator_step;
    uint8_t playback_speed_index;
} GameProfileV3;

typedef struct {
    char name[MACRO_FILENAME_MAX];
    uint8_t profile_index;
    uint8_t favorite;
    uint32_t recent_stamp;
} MacroPreference;

typedef enum {
    UiLanguageEnglish,
    UiLanguageSpanish,
    UiLanguageCount,
} UiLanguage;

typedef enum {
    ControllerLayoutSwitch,
    ControllerLayoutPlayStation,
    ControllerLayoutXbox,
} ControllerLayout;

typedef enum {
    FaceButtonTop,
    FaceButtonLeft,
    FaceButtonRight,
    FaceButtonBottom,
} FaceButtonPosition;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t active_profile;
    GameProfile profiles[PROFILE_COUNT];
    char profile_names[PROFILE_COUNT][PROFILE_NAME_MAX + 1U];
    uint32_t recent_clock;
    MacroPreference macros[MACRO_PREF_COUNT];
    uint8_t language;
    uint8_t controller_layout;
    uint8_t sound_enabled;
    uint8_t keep_screen_on;
    uint8_t recording_limit_index;
    uint8_t ui_reserved;
} AppSettings;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t active_profile;
    GameProfileV3 profiles[PROFILE_COUNT];
    char profile_names[PROFILE_COUNT][PROFILE_NAME_MAX + 1U];
    uint32_t recent_clock;
    MacroPreference macros[MACRO_PREF_COUNT];
} AppSettingsV2;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t active_profile;
    GameProfileV3 profiles[PROFILE_COUNT];
    char profile_names[PROFILE_COUNT][PROFILE_NAME_MAX + 1U];
    uint32_t recent_clock;
    MacroPreference macros[MACRO_PREF_COUNT];
    uint8_t language;
    uint8_t controller_layout;
    uint8_t sound_enabled;
    uint8_t keep_screen_on;
    uint8_t ui_reserved[2];
} AppSettingsV3;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t active_profile;
    GameProfileV3 profiles[PROFILE_COUNT];
} LegacyAppSettings;

typedef struct {
    FuriMessageQueue* input_queue;
    ViewPort* view_port;
    Gui* gui;
    NotificationApp* notification;
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
    volatile LiveUiScreen live_ui_screen;
    LiveUiScreen live_controller_return_screen;
    uint8_t live_recorder_selection;
    volatile BtStatus bt_status;
    volatile RunState state;
    volatile uint16_t completed_steps;
    volatile uint16_t playback_step;
    volatile uint32_t macro_elapsed_ms;
    volatile uint32_t macro_cycles;
    volatile uint32_t macro_playback_total_ms;
    uint32_t macro_total_ms;
    volatile bool macro_loop;
    volatile bool macro_paused;
    volatile uint8_t macro_speed_index;
    uint8_t macro_repeat_count;
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
    volatile bool live_first_frame_logged;
    volatile bool live_first_input_logged;
    volatile uint16_t live_reports_sent;
    volatile uint16_t live_frames_dropped;
    char* live_record_text;
    uint16_t live_record_bytes;
    uint16_t live_record_capacity;
    volatile uint16_t live_record_steps;
    volatile bool live_recording;
    volatile bool live_record_paused;
    volatile bool live_record_full;
    volatile bool live_record_limit_reached;
    volatile bool live_record_saved;
    char live_record_saved_name[MACRO_FILENAME_MAX];
    char live_record_suggested_name[MACRO_BASENAME_MAX + 1U];
    char* live_record_pending_text;
    uint16_t live_record_pending_bytes;
    uint16_t live_record_pending_steps;
    bool live_exit_after_prompt;
    SwitchReport live_record_state;
    SwitchReport live_latest_state;
    LiveRecentInput live_recent_inputs[LIVE_RECENT_INPUT_COUNT];
    uint8_t live_recent_count;
    bool live_record_has_state;
    bool live_record_has_input;
    bool live_latest_valid;
    uint32_t live_record_last_tick;
    uint32_t live_record_started_tick;
    volatile uint32_t live_record_elapsed_ms;
    MacroFile macro_files[MACRO_MAX_FILES];
    MacroBackup backups[BACKUP_MAX_FILES];
    uint8_t backup_count;
    uint8_t backup_index;
    uint8_t backup_offset;
    uint8_t backup_action_index;
    uint8_t backup_action_offset;
    uint8_t backup_conflict_selection;
    uint16_t backup_preview_step;
    uint16_t backup_existing_steps;
    uint32_t backup_existing_total_ms;
    MacroListMode backup_return_list_mode;
    uint32_t backup_preview_step_started;
    bool backup_preview_playing;
    bool backup_loaded;
    bool backup_from_live;
    bool backup_from_macro_list;
    char backup_restore_name[MACRO_BASENAME_MAX + 1U];
    uint8_t macro_count;
    uint8_t macro_total_count;
    uint8_t macro_index;
    uint8_t macro_list_offset;
    MacroListMode macro_list_mode;
    uint8_t macro_action_index;
    uint8_t macro_detail_selection;
    uint16_t macro_preview_step;
    uint16_t macro_view_step;
    uint8_t macro_profile_selection;
    uint16_t macro_editor_step;
    uint8_t macro_editor_selection;
    uint8_t macro_editor_button;
    bool macro_editor_dirty;
    uint8_t home_index;
    uint8_t exit_selection;
    uint8_t options_selection;
    uint8_t macro_play_selection;
    uint8_t profile_selection;
    uint8_t info_scroll;
    uint32_t info_mascot_last_step_tick;
    uint32_t info_mascot_last_frame_tick;
    uint32_t info_mascot_random_state;
    uint8_t info_mascot_x;
    uint8_t info_mascot_target_x;
    uint8_t info_mascot_frame;
    bool info_mascot_facing_left;
    bool info_mascot_initialized;
    uint8_t calibration_selection;
    bool calibration_right_stick;
    AppSettings settings;
    uint8_t simulator_selection;
    uint8_t simulator_held_control;
    SwitchReport simulator_report;
    bool simulator_error;
    bool simulator_stick_active;
    bool dev_fake_usb;
    bool dev_fake_usb_session;
    bool usb_flash_requested;
    uint32_t usb_flash_started_tick;
    uint8_t flash_held_keys;
    bool error_logging_enabled;
    bool error_log_has_previous;
    uint32_t error_log_last_tick;
    char error_log_last_event[28];
    char last_ui_message[32];
    volatile bool error_log_pending;
    volatile TxFailure error_log_pending_failure;
    MacroUiScreen macro_ui_screen;
    char rename_name[MACRO_BASENAME_MAX + 1U];
    uint8_t rename_position;
    uint8_t rename_keyboard_row;
    uint8_t rename_keyboard_column;
    bool rename_symbols;
    bool rename_lowercase;
    bool rename_cursor_selected;
    RenameTarget rename_target;
    uint8_t rename_profile_index;
    char ui_message[32];
} SwitchMacroApp;

static void refresh_macro_files(SwitchMacroApp* app);

static const NotificationMessage error_sound_on = {
    .type = NotificationMessageTypeSoundOn,
    .data.sound = {.frequency = 740.0f, .volume = 0.65f},
};
static const NotificationMessage error_sound_off = {.type = NotificationMessageTypeSoundOff};
static const NotificationSequence error_sound_sequence = {
    &error_sound_on,
    &message_delay_100,
    &error_sound_off,
    NULL,
};

static UiLanguage ui_language = UiLanguageEnglish;

typedef struct {
    const char* english;
    const char* translated;
} UiTranslation;

static const UiTranslation ui_translations[] = {
    {"Options", "Opciones"},
    {"OPTIONS", "OPCIONES"},
    {"Language", "Idioma"},
    {"Files", "archivos"},
    {"Time", "Tiempo"},
    {"Button layout", "Tipo de mando"},
    {"Keep screen on", "Mantener luz"},
    {"Sound", "Sonido"},
    {"Rec time limit", "Limite grabacion"},
    {"10 m", "10 m"},
    {"30 m", "30 m"},
    {"45 m", "45 m"},
    {"60 m", "60 m"},
    {"Dev Fake USB", "USB de prueba"},
    {"Logs", "Registros"},
    {"Info", "Informacion"},
    {"Created by Syzygy", "Creado por Syzygy"},
    {"PC software on GitHub", "Software de PC en Github"},
    {"Test stick", "Probar stick"},
    {"Save", "Guardar"},
    {"Reset", "Restablecer"},
    {"Stick", "Stick"},
    {"Left", "Izquierdo"},
    {"Right", "Derecho"},
    {"Arrows change value", "Flechas cambian valor"},
    {"Left/Right: ±1", "Izquierda/derecha: ±1"},
    {"Save macro as", "Guardar como"},
    {"Rename", "Renombrar"},
    {"Discard", "Descartar"},
    {"Save recording", "Guardar grabacion"},
    {"Save recording first", "Guarda la grabacion primero"},
    {"RENAME RECORDING", "RENOMBRAR GRABACION"},
    {"Backup", "Papelera"},
    {"Don't save", "No guardar"},
    {"Preview controller", "Ver mando"},
    {"Test on device", "Probar en dispositivo"},
    {"Restore", "Restaurar"},
    {"Play on Switch", "Reproducir en Switch"},
    {"Rename & restore", "Renombrar y restaurar"},
    {"Delete backup", "Eliminar copia"},
    {"Delete backup?", "¿Eliminar copia?"},
    {"Backup playback complete", "Reproducción completada"},
    {"Backup playback stopped", "Reproducción detenida"},
    {"Backup playback failed", "Error al reproducir"},
    {"Back to details", "Volver a detalles"},
    {"Saved in Backup", "Guardado en papelera"},
    {"Backup unavailable", "Copia no disponible"},
    {"Delete from Backup", "Eliminar de papelera"},
    {"No backups yet", "La papelera está vacía"},
    {"Move to Backup?", "¿Mover a la papelera?"},
    {"MOVE TO BACKUP", "MOVER A LA PAPELERA"},
    {"Overwrite", "Sobrescribir"},
    {"Add _2", "Añadir _2"},
    {"Cancel", "Cancelar"},
    {"Macro moved to Backup", "Macro movida a la papelera"},
    {"Macro restored", "Macro restaurada"},
    {"Backup deleted", "Copia eliminada"},
    {"RENAME RESTORED MACRO", "RENOMBRAR MACRO RESTAURADA"},
    {"EXISTING", "EXISTENTE"},
    {"IN BACKUP", "EN PAPELERA"},
    {"Start macro", "Iniciar macro"},
    {"Pause macro", "Pausar macro"},
    {"Resume macro", "Reanudar macro"},
    {"View controller", "Ver mando"},
    {"Step", "Paso"},
    {"Elapsed", "Transcurrido"},
    {"Total", "Total"},
    {"GitHub:", "GitHub:"},
    {"Change profile", "Cambiar perfil"},
    {"Deadzone", "Zona muerta"},
    {"Gain", "Ganancia"},
    {"Simulator step", "Paso de simulador"},
    {"Play speed", "Velocidad"},
    {"Calibrate sticks", "Calibrar sticks"},
    {"Rename profile", "Renombrar perfil"},
    {"Exit?", "¿Salir?"},
    {"Do you want to exit?", "¿Quieres salir?"},
    {"Stay", "Quedarse"},
    {"Exit", "Salir"},
    {"English", "Ingles"},
    {"On", "Si"},
    {"Off", "No"},
    {"Macros", "Macros"},
    {"Live controller", "Control en vivo"},
    {"Simulator", "Simulador"},
    {"Game profiles", "Perfiles de juego"},
    {"ALL", "TODAS"},
    {"FAVS", "FAVS"},
    {"RECENT", "RECI."},
    {"NO FAVORITES", "SIN FAVORITOS"},
    {"NO RECENT MACROS", "SIN RECIENTES"},
    {"NO MACROS", "SIN MACROS"},
    {"Add .txt files to SD", "Añade .txt a la SD"},
    {"Change tab with left/right", "Cambia filtro L/R"},
    {"Loop", "Bucle"},
    {"Repeats", "Repeticiones"},
    {"< On >", "< Si >"},
    {"< Off >", "< No >"},
    {"Play macro", "Reproducir macro"},
    {"Manage macro", "Gestionar macro"},
    {"Preview steps", "Ver pasos"},
    {"Edit macro", "Editar macro"},
    {"Assign profile", "Asignar perfil"},
    {"Add favorite", "Añadir favorito"},
    {"Remove favorite", "Quitar favorito"},
    {"Rename file", "Renombrar archivo"},
    {"Delete file", "Borrar archivo"},
    {"REMOVE", "BORRAR"},
    {"DELETE PERMANENTLY", "BORRAR PARA SIEMPRE"},
    {"Duration", "Duracion"},
    {"Steps", "Pasos"},
    {"DURATION", "DURACION"},
    {"STEPS", "PASOS"},
    {"STOPPED AT", "DETENIDA EN"},
    {"Start recording", "Iniciar grabación"},
    {"Stop and save", "Parar y guardar"},
    {"Record again", "Grabar otra vez"},
    {"Pause recording", "Pausar grabación"},
    {"Resume recording", "Reanudar grabación"},
    {"READY TO RECORD", "LISTO PARA GRABAR"},
    {"RECORDING PAUSED", "GRABACION EN PAUSA"},
    {"RECORDING INPUT", "GRABANDO MANDO"},
    {"STORAGE LIMIT REACHED", "LIMITE DE ESPACIO"},
    {"TIME LIMIT REACHED", "LIMITE DE TIEMPO"},
    {"USB SEND ERROR", "ERROR USB"},
    {"LEFT STICK", "STICK IZQUIERDO"},
    {"RIGHT STICK", "STICK DERECHO"},
    {"X OUT", "SALIDA X"},
    {"Y OUT", "SALIDA Y"},
    {"CHANGE PROFILE", "CAMBIAR PERFIL"},
    {"CALIBRATE STICKS", "CALIBRAR STICKS"},
    {"RENAME PROFILE", "RENOMBRAR PERFIL"},
    {"RENAME MACRO", "RENOMBRAR MACRO"},
    {"ASSIGN PROFILE", "ASIGNAR PERFIL"},
    {"AUTO / ACTIVE", "AUTO / ACTIVO"},
    {"NO STEPS TO PREVIEW", "SIN PASOS QUE MOSTRAR"},
    {"NO STEPS TO EDIT", "SIN PASOS QUE EDITAR"},
    {"WAIT / RELEASE", "ESPERA / SOLTAR"},
    {"DUPLICATE STEP", "DUPLICAR PASO"},
    {"DELETE STEP", "BORRAR PASO"},
    {"SAVE AND EXIT", "GUARDAR Y SALIR"},
    {"NONE", "NINGUNO"},
    {"UP", "ARRIBA"},
    {"DOWN", "ABAJO"},
    {"LEFT", "IZQUIERDA"},
    {"RIGHT", "DERECHA"},
    {"UP RIGHT", "ARRIBA DERECHA"},
    {"DOWN RIGHT", "ABAJO DERECHA"},
    {"DOWN LEFT", "ABAJO IZQUIERDA"},
    {"UP LEFT", "ARRIBA IZQUIERDA"},
    {"TAP", "PULSAR"},
    {"HOLD", "MANTENER"},
    {"REMOVE", "BORRAR"},
    {"SYM", "SIM"},
    {"DEL", "BOR"},
    {"SPC", "ESP"},
    {"SAVE", "GUAR"},
    {"USB DISCONNECTED", "USB DESCONECTADO"},
    {"STORAGE UNAVAILABLE", "ALMACENAMIENTO NO DISPONIBLE"},
    {"Storage unavailable", "Almacenamiento no disponible"},
    {"Not enough memory", "Memoria insuficiente"},
    {"SD folder unavailable", "Carpeta SD no disponible"},
    {"Macro recovery failed", "Error al recuperar macros"},
    {"No input to save", "No hay entrada que guardar"},
    {"SD unavailable", "SD no disponible"},
    {"SD write failed", "Error al escribir en SD"},
    {"Macro file not found", "No se encontró el archivo"},
    {"Macro file over 48 KB", "Archivo de macro mayor de 48 KB"},
    {"Macro file has no steps", "El archivo no tiene pasos"},
    {"Could not read macro file", "No se pudo leer la macro"},
    {"Invalid macro syntax", "Sintaxis de macro no válida"},
    {"More than 1024 steps", "Más de 1024 pasos"},
    {"Macro loaded", "Macro cargada"},
    {"Macro load error", "Error al cargar la macro"},
    {"Cannot clear backup", "No se pudo borrar la copia"},
    {"Cannot stage old macro", "No se pudo preparar la macro anterior"},
    {"Cannot replace macro", "No se pudo reemplazar la macro"},
    {"Macro saved", "Macro guardada"},
    {"Macro is full", "La macro está llena"},
    {"Keep one macro step", "Debe quedar al menos un paso"},
    {"Delete failed", "Error al borrar"},
    {"Macro deleted", "Macro borrada"},
    {"No macros left", "No quedan macros"},
    {"Name cannot be empty", "El nombre no puede estar vacio"},
    {"Name is too long", "El nombre es demasiado largo"},
    {"Name already exists", "Ese nombre ya existe"},
    {"Rename failed", "Error al renombrar"},
    {"Macro renamed", "Macro renombrada"},
    {"Name must be 1-9 chars", "El nombre debe tener 1-9 caracteres"},
    {"Name already used", "Ese nombre ya está en uso"},
    {"Settings save failed", "Error al guardar ajustes"},
    {"Preference list full", "Lista de preferencias llena"},
    {"Added to favorites", "Añadida a favoritos"},
    {"Removed from favorites", "Quitada de favoritos"},
    {"USB mode change failed", "Error al cambiar el modo USB"},
    {"Pause unavailable", "No se puede pausar"},
    {"Start recording first", "Inicia la grabación primero"},
    {"Record unavailable", "No se puede grabar"},
    {"USB disconnected", "USB desconectado"},
    {"USB busy timeout", "Tiempo agotado: USB ocupado"},
    {"USB config failed", "Error de configuración USB"},
    {"USB write rejected", "USB rechazó el envío"},
    {"USB ACK timeout", "Tiempo agotado: respuesta USB"},
    {"USB ready", "USB listo"},
    {"USB error", "Error USB"},
};

#include "ui_unicode_glyphs.h"
#include "info_mascot_frames.h"

static const char* ui_translate_static(const char* text) {
    if(ui_language == UiLanguageEnglish) return text;
    if(ui_language == UiLanguageSpanish) {
        for(size_t i = 0U; i < COUNT_OF(ui_translations); i++) {
            if(strcmp(text, ui_translations[i].english) == 0) return ui_translations[i].translated;
        }
    }
    return text;
}

static const char* ui_translate(const char* text) {
    if(!text) return text;
    const char* static_translation = ui_translate_static(text);
    if(static_translation != text) return static_translation;
    if(ui_language == UiLanguageEnglish) return text;

    static char translated[96];
    unsigned int first = 0U;
    unsigned int second = 0U;
    char value[32] = {0};
    char label[32] = {0};
    char label2[32] = {0};
    char trailing = '\0';
    if(sscanf(text, "%u %31s%c", &first, value, &trailing) == 2 &&
       strcmp(value, "Files") == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("Files"));
        snprintf(translated, sizeof(translated), "%u %s", first, label);
        return translated;
    }
    if(sscanf(text, "Steps %u  Time %31s", &first, value) == 2) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("Steps"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static("Time"));
        snprintf(translated, sizeof(translated), "%.20s %u  %.20s %.16s", label, first, label2, value);
        return translated;
    }
    if(sscanf(text, "Steps %u  ·  Time %31s", &first, value) == 2) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("Steps"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static("Time"));
        snprintf(
            translated,
            sizeof(translated),
            "%.20s %u  ·  %.20s %.16s",
            label,
            first,
            label2,
            value);
        return translated;
    }
    if(sscanf(text, "Loop: %15s  ·  Repeats: %u", value, &first) == 2) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("Loop"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static("Repeats"));
        snprintf(
            translated,
            sizeof(translated),
            "%s: %s  ·  %s: %u",
            label,
            strcmp(value, "On") == 0 ? ui_translate_static("On") : ui_translate_static("Off"),
            label2,
            first);
        return translated;
    }
    if(strncmp(text, "PROFILE  ", 9U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("PROFILE"));
        snprintf(translated, sizeof(translated), "%s  %.31s", label, text + 9U);
        return translated;
    }
    if(sscanf(text, "STEP %u/%u", &first, &second) == 2) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("STEP"));
        snprintf(translated, sizeof(translated), "%s %u/%u", label, first, second);
        return translated;
    }
    if(sscanf(text, "STEP  %u", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("STEP"));
        snprintf(translated, sizeof(translated), "%s  %03u", label, first);
        return translated;
    }
    if(sscanf(text, "EDIT STEP %u/%u", &first, &second) == 2) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("EDIT STEP"));
        snprintf(translated, sizeof(translated), "%s %u/%u", label, first, second);
        return translated;
    }
    if(sscanf(text, "BUTTON %31s", value) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("BUTTON"));
        snprintf(
            translated,
            sizeof(translated),
            "%s %s",
            label,
            strcmp(value, "NONE") == 0 ? ui_translate_static("NONE") : value);
        return translated;
    }
    if(strncmp(text, "DPAD  ", 6U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("DPAD"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static(text + 6U));
        snprintf(translated, sizeof(translated), "%s  %s", label, label2);
        return translated;
    }
    if(strncmp(text, "DPAD ", 5U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("DPAD"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static(text + 5U));
        snprintf(translated, sizeof(translated), "%s %s", label, label2);
        return translated;
    }
    if(sscanf(text, "TIME  %u ms", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("TIME"));
        snprintf(translated, sizeof(translated), "%s  %u ms", label, first);
        return translated;
    }
    if(sscanf(text, "MODE  %31s", value) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("MODE"));
        snprintf(
            label2,
            sizeof(label2),
            "%s",
            ui_translate_static(strcmp(value, "HOLD") == 0 ? "HOLD" : "TAP"));
        snprintf(translated, sizeof(translated), "%s  %s", label, label2);
        return translated;
    }
    if(sscanf(text, "DEADZONE  %u", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("DEADZONE"));
        snprintf(translated, sizeof(translated), "%s  %02u", label, first);
        return translated;
    }
    if(sscanf(text, "GAIN     %u%%", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("Gain"));
        snprintf(translated, sizeof(translated), "%s  %u%%", label, first);
        return translated;
    }
    if(sscanf(text, "SIM STEP  %u", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("SIM STEP"));
        snprintf(translated, sizeof(translated), "%s %u", label, first);
        return translated;
    }
    if(sscanf(text, "PLAY SPEED %u%%", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("PLAY SPEED"));
        snprintf(translated, sizeof(translated), "%s %u%%", label, first);
        return translated;
    }
    if(sscanf(text, "GAIN %u%%  DZ %u", &first, &second) == 2) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("Gain"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static("DEADZONE"));
        snprintf(translated, sizeof(translated), "%s %u%%  %s %u", label, first, label2, second);
        return translated;
    }
    if(sscanf(text, "ENDPOINT %x", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("ENDPOINT"));
        snprintf(translated, sizeof(translated), "%s %02X", label, first);
        return translated;
    }
    if(sscanf(text, "STEP %u FAILED", &first) == 1) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("STEP"));
        snprintf(label2, sizeof(label2), "%s", ui_translate_static("FAILED"));
        snprintf(translated, sizeof(translated), "%s %u %s", label, first, label2);
        return translated;
    }
    if(strncmp(text, "SAVED ", 6U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("SAVED"));
        snprintf(translated, sizeof(translated), "%s %.18s", label, text + 6U);
        return translated;
    }
    if(strncmp(text, "Saved ", 6U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("SAVED"));
        snprintf(translated, sizeof(translated), "%s %.24s", label, text + 6U);
        return translated;
    }
    if(strncmp(text, "HOLD  ", 6U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("HOLD"));
        snprintf(translated, sizeof(translated), "%s  %.31s", label, text + 6U);
        return translated;
    }
    if(strncmp(text, "TAP  ", 5U) == 0) {
        snprintf(label, sizeof(label), "%s", ui_translate_static("TAP"));
        snprintf(translated, sizeof(translated), "%s  %.31s", label, text + 5U);
        return translated;
    }
    return text;
}

static uint16_t ui_next_utf8(const char** text) {
    const uint8_t first = (uint8_t)*(*text)++;
    if(first < 0x80U) return first;
    if((first & 0xE0U) == 0xC0U) {
        const uint8_t second = (uint8_t)*(*text)++;
        return (uint16_t)(((first & 0x1FU) << 6U) | (second & 0x3FU));
    }
    if((first & 0xF0U) == 0xE0U) {
        const uint8_t second = (uint8_t)*(*text)++;
        const uint8_t third = (uint8_t)*(*text)++;
        return (uint16_t)(((first & 0x0FU) << 12U) | ((second & 0x3FU) << 6U) | (third & 0x3FU));
    }
    if((first & 0xF8U) == 0xF0U) {
        (*text)++;
        (*text)++;
        (*text)++;
        return '?';
    }
    return '?';
}

static const UiUnicodeGlyph* ui_unicode_glyph(uint16_t codepoint) {
    size_t low = 0U;
    size_t high = COUNT_OF(ui_unicode_glyphs);
    while(low < high) {
        const size_t middle = low + (high - low) / 2U;
        if(ui_unicode_glyphs[middle].codepoint == codepoint) return &ui_unicode_glyphs[middle];
        if(ui_unicode_glyphs[middle].codepoint < codepoint) low = middle + 1U;
        else high = middle;
    }
    return NULL;
}

static uint16_t ui_text_width(Canvas* canvas, const char* text) {
    uint16_t width = 0U;
    while(*text) {
        const uint16_t codepoint = ui_next_utf8(&text);
        width = (uint16_t)(width + (codepoint < 0x80U ? canvas_glyph_width(canvas, codepoint) : 8U));
    }
    return width;
}

static void ui_draw_text(Canvas* canvas, int32_t x, int32_t baseline, const char* text) {
    bool has_unicode = false;
    for(const char* scan = text; *scan;) {
        if(ui_next_utf8(&scan) >= 0x80U) {
            has_unicode = true;
            break;
        }
    }
    if(!has_unicode) {
        canvas_draw_str(canvas, x, baseline, text);
        return;
    }

    while(*text) {
        const uint16_t codepoint = ui_next_utf8(&text);
        if(codepoint < 0x80U) {
            canvas_draw_glyph(canvas, x, baseline, codepoint);
            x += canvas_glyph_width(canvas, codepoint);
        } else {
            const UiUnicodeGlyph* glyph = ui_unicode_glyph(codepoint);
            const uint8_t* bitmap = glyph ? glyph->bitmap : NULL;
            if(bitmap) canvas_draw_xbm(canvas, x, baseline - 6, 8U, 8U, bitmap);
            else canvas_draw_glyph(canvas, x, baseline, '?');
            x += 8;
        }
    }
}

static bool ui_message_is_success(const char* message) {
    static const char* const success_messages[] = {
        "Macro saved",
        "Macro deleted",
        "No macros left",
        "Macro renamed",
        "Added to favorites",
        "Removed from favorites",
        "USB ready",
    };
    if(strncmp(message, "Saved ", 6U) == 0) return true;
    for(size_t i = 0U; i < COUNT_OF(success_messages); i++) {
        if(strcmp(message, success_messages[i]) == 0) return true;
    }
    return false;
}

static void play_error_sound(SwitchMacroApp* app) {
    if(app->settings.sound_enabled && app->notification) {
        notification_message(app->notification, &error_sound_sequence);
    }
}

static void set_screen_awake(SwitchMacroApp* app, bool enabled) {
    if(!app->notification) return;
    notification_message(
        app->notification,
        enabled ? &sequence_display_backlight_enforce_on : &sequence_display_backlight_enforce_auto);
}

static void update_ui_error_sound(SwitchMacroApp* app) {
    if(!app->ui_message[0]) {
        app->last_ui_message[0] = '\0';
        return;
    }
    if(strcmp(app->ui_message, app->last_ui_message) == 0) return;

    snprintf(app->last_ui_message, sizeof(app->last_ui_message), "%s", app->ui_message);
    if(!ui_message_is_success(app->ui_message)) play_error_sound(app);
}

static void trim_utf8_tail(char* text, size_t* length) {
    if(*length == 0U) return;
    size_t position = *length - 1U;
    while(position > 0U && (((uint8_t)text[position] & 0xC0U) == 0x80U)) position--;
    text[position] = '\0';
    *length = position;
}

#define UI_STR(canvas, x, y, text) ui_draw_text(canvas, x, y, ui_translate(text))

static GameProfile* active_profile(SwitchMacroApp* app) {
    if(app->settings.active_profile >= PROFILE_COUNT) app->settings.active_profile = 0U;
    return &app->settings.profiles[app->settings.active_profile];
}

static const GameProfile* active_profile_const(const SwitchMacroApp* app) {
    uint8_t index = app->settings.active_profile;
    if(index >= PROFILE_COUNT) index = 0U;
    return &app->settings.profiles[index];
}

static uint8_t transform_axis(uint8_t value, uint8_t deadzone_value, uint8_t gain_percent) {
    const int16_t delta = (int16_t)value - 128;
    const bool negative = delta < 0;
    const uint16_t magnitude = negative ? (uint16_t)-delta : (uint16_t)delta;
    const uint16_t side_limit = negative ? 128U : 127U;
    const uint16_t deadzone = deadzone_value < side_limit ? deadzone_value : side_limit - 1U;
    if(magnitude <= deadzone) return 0x80U;

    uint16_t scaled = (uint16_t)(((uint32_t)(magnitude - deadzone) * side_limit) /
                                 (side_limit - deadzone));
    scaled = (uint16_t)(((uint32_t)scaled * gain_percent + 50U) / 100U);
    if(scaled > side_limit) scaled = side_limit;
    return (uint8_t)(negative ? 128U - scaled : 128U + scaled);
}

static uint32_t playback_duration_ms(uint32_t source_ms, uint8_t speed_index) {
    if(speed_index >= sizeof(playback_speeds)) speed_index = 2U;
    const uint32_t speed = playback_speeds[speed_index];
    return (uint32_t)(((uint64_t)source_ms * 100U + speed / 2U) / speed);
}

static void settings_set_defaults(AppSettings* settings) {
    memset(settings, 0, sizeof(*settings));
    settings->magic = SWITCH_MACRO_SETTINGS_MAGIC;
    settings->version = 4U;
    settings->language = UiLanguageEnglish;
    settings->controller_layout = ControllerLayoutSwitch;
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        settings->profiles[i].left_deadzone = profile_deadzones[0];
        settings->profiles[i].right_deadzone = profile_deadzones[0];
        settings->profiles[i].left_gain_percent = profile_gains[0];
        settings->profiles[i].right_gain_percent = profile_gains[0];
        settings->profiles[i].simulator_step = profile_simulator_steps[2];
        settings->profiles[i].playback_speed_index = 2U;
        snprintf(
            settings->profile_names[i],
            sizeof(settings->profile_names[i]),
            "%s",
            default_profile_names[i]);
    }
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        settings->macros[i].profile_index = MACRO_NO_PROFILE;
    }
}

static bool byte_in_values(uint8_t value, const uint8_t* values, size_t count) {
    for(size_t i = 0U; i < count; i++) {
        if(values[i] == value) return true;
    }
    return false;
}

static bool settings_valid(const AppSettings* settings) {
    if(settings->magic != SWITCH_MACRO_SETTINGS_MAGIC || settings->version != 4U ||
       settings->active_profile >= PROFILE_COUNT || settings->language >= UiLanguageCount ||
       settings->controller_layout > ControllerLayoutXbox || settings->sound_enabled > 1U ||
       settings->keep_screen_on > 1U || settings->recording_limit_index >= RECORDING_LIMIT_COUNT) {
        return false;
    }
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        const GameProfile* profile = &settings->profiles[i];
        if(profile->left_deadzone > 32U || profile->right_deadzone > 32U ||
           profile->left_gain_percent < 50U || profile->left_gain_percent > 200U ||
           profile->right_gain_percent < 50U || profile->right_gain_percent > 200U ||
           !byte_in_values(
               profile->simulator_step, profile_simulator_steps, sizeof(profile_simulator_steps)) ||
           profile->playback_speed_index >= sizeof(playback_speeds)) {
            return false;
        }
        if(!memchr(settings->profile_names[i], '\0', sizeof(settings->profile_names[i])) ||
           settings->profile_names[i][0] == '\0') {
            return false;
        }
    }
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        const MacroPreference* preference = &settings->macros[i];
        if(!memchr(preference->name, '\0', sizeof(preference->name)) ||
           (preference->profile_index != MACRO_NO_PROFILE &&
            preference->profile_index >= PROFILE_COUNT) ||
           preference->favorite > 1U) {
            return false;
        }
    }
    return true;
}

static bool settings_v2_valid(const AppSettingsV2* previous) {
    if(previous->magic != SWITCH_MACRO_SETTINGS_MAGIC || previous->version != 2U ||
       previous->active_profile >= PROFILE_COUNT) return false;
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        const GameProfileV3* profile = &previous->profiles[i];
        if(profile->deadzone > 32U || profile->gain_percent < 50U ||
           profile->gain_percent > 200U ||
           !byte_in_values(
               profile->simulator_step, profile_simulator_steps, sizeof(profile_simulator_steps)) ||
           profile->playback_speed_index >= sizeof(playback_speeds) ||
           !memchr(previous->profile_names[i], '\0', sizeof(previous->profile_names[i])) ||
           previous->profile_names[i][0] == '\0') return false;
    }
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        const MacroPreference* preference = &previous->macros[i];
        if(!memchr(preference->name, '\0', sizeof(preference->name)) ||
           (preference->profile_index != MACRO_NO_PROFILE &&
            preference->profile_index >= PROFILE_COUNT) || preference->favorite > 1U) return false;
    }
    return true;
}

static bool settings_v3_valid(const AppSettingsV3* previous) {
    if(previous->magic != SWITCH_MACRO_SETTINGS_MAGIC || previous->version != 3U ||
       previous->active_profile >= PROFILE_COUNT || previous->language >= 7U ||
       previous->controller_layout > ControllerLayoutXbox || previous->sound_enabled > 1U ||
       previous->keep_screen_on > 1U) return false;
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        const GameProfileV3* profile = &previous->profiles[i];
        if(profile->deadzone > 32U || profile->gain_percent < 50U ||
           profile->gain_percent > 200U ||
           !byte_in_values(
               profile->simulator_step, profile_simulator_steps, sizeof(profile_simulator_steps)) ||
           profile->playback_speed_index >= sizeof(playback_speeds) ||
           !memchr(previous->profile_names[i], '\0', sizeof(previous->profile_names[i])) ||
           previous->profile_names[i][0] == '\0') return false;
    }
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        const MacroPreference* preference = &previous->macros[i];
        if(!memchr(preference->name, '\0', sizeof(preference->name)) ||
           (preference->profile_index != MACRO_NO_PROFILE &&
            preference->profile_index >= PROFILE_COUNT) || preference->favorite > 1U) return false;
    }
    return true;
}

static bool legacy_settings_valid(const LegacyAppSettings* settings) {
    if(settings->magic != SWITCH_MACRO_SETTINGS_MAGIC || settings->version != 1U ||
       settings->active_profile >= PROFILE_COUNT) {
        return false;
    }
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        const GameProfileV3* profile = &settings->profiles[i];
        if(profile->deadzone > 32U || profile->gain_percent < 50U ||
           profile->gain_percent > 200U ||
           !byte_in_values(
               profile->simulator_step, profile_simulator_steps, sizeof(profile_simulator_steps)) ||
           profile->playback_speed_index >= sizeof(playback_speeds)) {
            return false;
        }
    }
    return true;
}

static MacroPreference* macro_preference_find(SwitchMacroApp* app, const char* name) {
    if(!name || !*name) return NULL;
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        MacroPreference* preference = &app->settings.macros[i];
        if(preference->name[0] && strcmp(preference->name, name) == 0) return preference;
    }
    return NULL;
}

static const MacroPreference* macro_preference_find_const(const SwitchMacroApp* app, const char* name) {
    if(!name || !*name) return NULL;
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        const MacroPreference* preference = &app->settings.macros[i];
        if(preference->name[0] && strcmp(preference->name, name) == 0) return preference;
    }
    return NULL;
}

static MacroPreference* macro_preference_get(SwitchMacroApp* app, const char* name) {
    MacroPreference* preference = macro_preference_find(app, name);
    if(preference) return preference;
    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        preference = &app->settings.macros[i];
        if(preference->name[0] == '\0') {
            memset(preference, 0, sizeof(*preference));
            snprintf(preference->name, sizeof(preference->name), "%s", name);
            preference->profile_index = MACRO_NO_PROFILE;
            return preference;
        }
    }
    return NULL;
}

static bool macro_is_favorite(const SwitchMacroApp* app, const char* name) {
    const MacroPreference* preference = macro_preference_find_const(app, name);
    return preference && preference->favorite != 0U;
}

static void macro_preference_rename(SwitchMacroApp* app, const char* old_name, const char* new_name) {
    MacroPreference* preference = macro_preference_find(app, old_name);
    if(preference) snprintf(preference->name, sizeof(preference->name), "%s", new_name);
}

static void macro_preference_remove(SwitchMacroApp* app, const char* name) {
    MacroPreference* preference = macro_preference_find(app, name);
    if(preference) {
        memset(preference, 0, sizeof(*preference));
        preference->profile_index = MACRO_NO_PROFILE;
    }
}

static const RenameKey rename_alpha_row_0[] = {
    {'Q', 6, 7, RenameKeyCharacter}, {'W', 14, 7, RenameKeyCharacter},
    {'E', 22, 7, RenameKeyCharacter}, {'R', 30, 7, RenameKeyCharacter},
    {'T', 38, 7, RenameKeyCharacter}, {'Y', 46, 7, RenameKeyCharacter},
    {'U', 54, 7, RenameKeyCharacter}, {'I', 62, 7, RenameKeyCharacter},
    {'O', 70, 7, RenameKeyCharacter}, {'P', 78, 7, RenameKeyCharacter},
    {'0', 91, 7, RenameKeyCharacter}, {'1', 100, 7, RenameKeyCharacter},
    {'2', 109, 7, RenameKeyCharacter}, {'3', 118, 7, RenameKeyCharacter},
};

static const RenameKey rename_alpha_row_1[] = {
    {'A', 10, 7, RenameKeyCharacter}, {'S', 18, 7, RenameKeyCharacter},
    {'D', 26, 7, RenameKeyCharacter}, {'F', 34, 7, RenameKeyCharacter},
    {'G', 42, 7, RenameKeyCharacter}, {'H', 50, 7, RenameKeyCharacter},
    {'J', 58, 7, RenameKeyCharacter}, {'K', 66, 7, RenameKeyCharacter},
    {'L', 74, 7, RenameKeyCharacter}, {0, 80, 17, RenameKeyBackspace},
    {'4', 101, 7, RenameKeyCharacter}, {'5', 110, 7, RenameKeyCharacter},
    {'6', 119, 7, RenameKeyCharacter},
};

static const RenameKey rename_alpha_row_2[] = {
    {0, 2, 11, RenameKeySymbols}, {'Z', 17, 7, RenameKeyCharacter},
    {'X', 25, 7, RenameKeyCharacter}, {'C', 33, 7, RenameKeyCharacter},
    {'V', 41, 7, RenameKeyCharacter}, {'B', 49, 7, RenameKeyCharacter},
    {'N', 57, 7, RenameKeyCharacter}, {'M', 65, 7, RenameKeyCharacter},
    {0, 72, 15, RenameKeyCase}, {0, 89, 14, RenameKeySave},
    {'7', 105, 7, RenameKeyCharacter},
    {'8', 113, 7, RenameKeyCharacter}, {'9', 121, 7, RenameKeyCharacter},
};

static const RenameKey rename_symbol_row_0[] = {
    {'!', 6, 7, RenameKeyCharacter}, {'@', 14, 7, RenameKeyCharacter},
    {'#', 22, 7, RenameKeyCharacter}, {'$', 30, 7, RenameKeyCharacter},
    {'%', 38, 7, RenameKeyCharacter}, {'&', 46, 7, RenameKeyCharacter},
    {'(', 54, 7, RenameKeyCharacter}, {')', 62, 7, RenameKeyCharacter},
    {'+', 70, 7, RenameKeyCharacter}, {'=', 78, 7, RenameKeyCharacter},
    {'0', 91, 7, RenameKeyCharacter}, {'1', 100, 7, RenameKeyCharacter},
    {'2', 109, 7, RenameKeyCharacter}, {'3', 118, 7, RenameKeyCharacter},
};

static const RenameKey rename_symbol_row_1[] = {
    {'~', 6, 7, RenameKeyCharacter}, {';', 14, 7, RenameKeyCharacter},
    {',', 22, 7, RenameKeyCharacter}, {'.', 30, 7, RenameKeyCharacter},
    {'[', 38, 7, RenameKeyCharacter}, {']', 46, 7, RenameKeyCharacter},
    {'{', 54, 7, RenameKeyCharacter}, {'}', 62, 7, RenameKeyCharacter},
    {0, 69, 27, RenameKeyBackspace}, {'4', 100, 7, RenameKeyCharacter},
    {'5', 109, 7, RenameKeyCharacter}, {'6', 118, 7, RenameKeyCharacter},
};

static const RenameKey rename_symbol_row_2[] = {
    {0, 2, 11, RenameKeySymbols}, {'-', 17, 7, RenameKeyCharacter},
    {'_', 25, 7, RenameKeyCharacter}, {'.', 33, 7, RenameKeyCharacter},
    {'@', 41, 7, RenameKeyCharacter}, {'!', 49, 7, RenameKeyCharacter},
    {'&', 57, 7, RenameKeyCharacter}, {'\'', 65, 7, RenameKeyCharacter},
    {0, 71, 14, RenameKeySpace}, {0, 86, 15, RenameKeySave},
    {'7', 105, 7, RenameKeyCharacter}, {'8', 113, 7, RenameKeyCharacter},
    {'9', 121, 7, RenameKeyCharacter},
};

static const RenameKey* const rename_alpha_rows[] = {
    rename_alpha_row_0, rename_alpha_row_1, rename_alpha_row_2};
static const uint8_t rename_alpha_row_sizes[] = {
    COUNT_OF(rename_alpha_row_0), COUNT_OF(rename_alpha_row_1), COUNT_OF(rename_alpha_row_2)};
static const RenameKey* const rename_symbol_rows[] = {
    rename_symbol_row_0, rename_symbol_row_1, rename_symbol_row_2};
static const uint8_t rename_symbol_row_sizes[] = {
    COUNT_OF(rename_symbol_row_0), COUNT_OF(rename_symbol_row_1), COUNT_OF(rename_symbol_row_2)};

static const RenameKey* get_rename_keyboard_row(const SwitchMacroApp* app, uint8_t row, uint8_t* size) {
    if(app->rename_symbols) {
        *size = rename_symbol_row_sizes[row];
        return rename_symbol_rows[row];
    }
    *size = rename_alpha_row_sizes[row];
    return rename_alpha_rows[row];
}

static SwitchMacroApp* active_app;
static bool keyboard_protocol_boot;

static bool finish_live_recording(SwitchMacroApp* app);
static bool save_pending_live_recording(SwitchMacroApp* app, const char* basename);
static SwitchReport neutral_gamepad(void);
static void write_error_log(SwitchMacroApp* app, const char* event);

static void wake_tx_waiters(SwitchMacroApp* app) {
    if(app->gamepad_tx_sem) furi_semaphore_release(app->gamepad_tx_sem);
    if(app->keyboard_tx_sem) furi_semaphore_release(app->keyboard_tx_sem);
}

static void record_tx_failure(SwitchMacroApp* app, TxFailure failure, uint8_t ep, int32_t result) {
    if(app->tx_failure == TxFailureNone) {
        app->tx_failure = failure;
        app->failed_endpoint = ep;
        app->usb_result = result;
        app->error_log_pending_failure = failure;
        app->error_log_pending = true;
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
            if(active_app->view_port) view_port_update(active_app->view_port);
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
        if(active_app->view_port) view_port_update(active_app->view_port);
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
    if(active_app) {
        active_app->usb_dev = NULL;
        active_app->configured = false;
        active_app->connected = false;
        active_app->controller_wake_pending = false;
        wake_tx_waiters(active_app);
        if(active_app->view_port) view_port_update(active_app->view_port);
    }
}

static void usb_suspend(usbd_device* dev) {
    UNUSED(dev);
    if(active_app) {
        active_app->connected = false;
        active_app->controller_wake_pending = false;
        wake_tx_waiters(active_app);
        if(active_app->view_port) view_port_update(active_app->view_port);
    }
}

static void usb_wakeup(usbd_device* dev) {
    UNUSED(dev);
    if(active_app) {
        active_app->connected = active_app->configured;
        if(active_app->configured) active_app->controller_wake_pending = true;
        if(active_app->view_port) view_port_update(active_app->view_port);
    }
}

static void usb_state_callback(FuriHalUsbStateEvent event, void* ctx) {
    SwitchMacroApp* app = ctx;
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
    if(app->view_port) view_port_update(app->view_port);
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

static FuriHalUsbInterface switch_macro_usb = {
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

static bool set_dev_fake_usb_mode(SwitchMacroApp* app, bool enabled) {
    if(app->dev_fake_usb == enabled) return true;
    if(enabled) {
        if(!furi_hal_usb_set_config(NULL, NULL)) return false;
        app->dev_fake_usb = true;
        app->connected = false;
        app->configured = false;
        app->controller_wake_pending = false;
    } else {
        if(!furi_hal_usb_set_config(&switch_macro_usb, NULL)) return false;
        app->dev_fake_usb = false;
    }
    if(app->view_port) view_port_update(app->view_port);
    return true;
}

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
    const uint8_t ellipsis_width = ui_text_width(canvas, "...");
    size_t length = strlen(line);
    bool truncated = false;
    while(length > 0U &&
          (length > sizeof(line) - 4U ||
          ui_text_width(canvas, line) + ellipsis_width > available_width)) {
        line[--length] = '\0';
        truncated = true;
    }
    if(truncated) memcpy(line + length, "...", 4U);
    ui_draw_text(canvas, x, y, line);
}

static void draw_macro_filename(Canvas* canvas, const char* name, int16_t y) {
    draw_macro_filename_at(canvas, 4U, name, y);
}

static void normalize_macro_list_offset(SwitchMacroApp* app) {
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

static void draw_usb_status_icon(Canvas* canvas, uint8_t x, uint8_t y, bool enabled) {
    // Compact 9x9 USB trident: arrow, round terminal, square terminal and base node.
    canvas_draw_dot(canvas, x + 4U, y);
    canvas_draw_line(canvas, x + 3U, y + 1U, x + 5U, y + 1U);
    canvas_draw_line(canvas, x + 2U, y + 2U, x + 6U, y + 2U);
    canvas_draw_line(canvas, x + 4U, y + 2U, x + 4U, y + 7U);

    canvas_draw_line(canvas, x + 4U, y + 5U, x + 7U, y + 2U);
    canvas_draw_box(canvas, x + 6U, y + 1U, 3U, 3U);

    canvas_draw_line(canvas, x + 1U, y + 4U, x + 4U, y + 6U);
    canvas_draw_line(canvas, x + 1U, y + 4U, x + 1U, y + 5U);
    canvas_draw_disc(canvas, x + 1U, y + 4U, 1U);
    canvas_draw_disc(canvas, x + 4U, y + 7U, 1U);

    if(!enabled) canvas_draw_line(canvas, x, y + 8U, x + 8U, y);
}

static void draw_usb_status_icon_mode(Canvas* canvas, const SwitchMacroApp* app, uint8_t x, uint8_t y) {
    draw_usb_status_icon(canvas, x, y, app->connected);
    if(app->dev_fake_usb) canvas_draw_circle(canvas, x + 4U, y + 4U, 5U);
}

static void draw_bluetooth_status_icon(Canvas* canvas, uint8_t x, uint8_t y, bool enabled) {
    // Bluetooth rune based on the supplied silhouette, with a single-pixel off slash.
    canvas_draw_line(canvas, x + 3U, y, x + 3U, y + 8U);
    canvas_draw_line(canvas, x + 3U, y, x + 6U, y + 2U);
    canvas_draw_line(canvas, x + 6U, y + 2U, x + 3U, y + 4U);
    canvas_draw_line(canvas, x + 3U, y + 4U, x + 6U, y + 6U);
    canvas_draw_line(canvas, x + 6U, y + 6U, x + 3U, y + 8U);
    canvas_draw_line(canvas, x, y + 2U, x + 6U, y + 6U);
    canvas_draw_line(canvas, x, y + 6U, x + 6U, y + 2U);
    if(!enabled) canvas_draw_line(canvas, x, y + 8U, x + 7U, y);
}

static void draw_battery_status_icon(Canvas* canvas, uint8_t x, uint8_t y) {
    const uint8_t charge = furi_hal_power_get_pct();
    canvas_draw_frame(canvas, x, y + 1U, 9U, 7U);
    canvas_draw_box(canvas, x + 9U, y + 3U, 1U, 3U);
    uint8_t fill_width = (uint8_t)(((uint16_t)charge * 5U + 99U) / 100U);
    if(fill_width > 5U) fill_width = 5U;
    if(fill_width > 0U) canvas_draw_box(canvas, x + 2U, y + 3U, fill_width, 3U);
}

static bool main_usb_icon_visible(const SwitchMacroApp* app);

static void draw_connection_status_icons(Canvas* canvas, const SwitchMacroApp* app) {
    if(!main_usb_icon_visible(app)) return;
    draw_usb_status_icon_mode(canvas, app, 107U, 0U);
    draw_bluetooth_status_icon(canvas, 119U, 0U, app->bt_status == BtStatusConnected);
}

static bool main_usb_icon_visible(const SwitchMacroApp* app) {
    if(!app->usb_flash_requested) return true;
    const uint32_t elapsed = furi_get_tick() - app->usb_flash_started_tick;
    const uint32_t half_period = furi_ms_to_ticks(ERROR_BLINK_HALF_PERIOD_MS);
    if(elapsed >= furi_ms_to_ticks(ERROR_BLINK_DURATION_MS) || half_period == 0U) return true;
    return ((elapsed / half_period) & 1U) == 0U;
}

static uint8_t flash_key_mask(InputKey key) {
    switch(key) {
    case InputKeyUp: return 1U << 0U;
    case InputKeyDown: return 1U << 1U;
    case InputKeyLeft: return 1U << 2U;
    case InputKeyRight: return 1U << 3U;
    case InputKeyOk: return 1U << 4U;
    case InputKeyBack: return 1U << 5U;
    default: return 0U;
    }
}

static void track_flash_input(SwitchMacroApp* app, const InputEvent* event) {
    const uint8_t mask = flash_key_mask(event->key);
    if(mask == 0U) return;
    if(event->type == InputTypePress) {
        app->flash_held_keys |= mask;
    } else if(event->type == InputTypeRelease) {
        app->flash_held_keys &= (uint8_t)~mask;
    }
}

static void request_usb_error_flash(SwitchMacroApp* app, InputKey initiating_key) {
    if(!app->usb_flash_requested) {
        app->usb_flash_requested = true;
        app->usb_flash_started_tick = furi_get_tick();
        play_error_sound(app);
    }
    app->flash_held_keys |= flash_key_mask(initiating_key);
    view_port_update(app->view_port);
}

static void draw_header_band(Canvas* canvas, uint8_t x, uint8_t y, uint8_t width, uint8_t height) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rbox(canvas, x, y, width, height, 2U);
}

static void draw_page_header_raw(
    Canvas* canvas,
    const char* title,
    uint8_t max_width,
    uint8_t height,
    uint8_t baseline,
    Font font) {
    char fitted[64];
    snprintf(fitted, sizeof(fitted), "%s", title);
    canvas_set_font(canvas, font);
    const size_t original_length = strlen(fitted);
    size_t length = strlen(fitted);
    const uint8_t text_limit = max_width > 8U ? max_width - 8U : 0U;
    while(length > 0U && ui_text_width(canvas, fitted) > text_limit) {
        trim_utf8_tail(fitted, &length);
    }
    if(length < original_length && length > 3U) {
        trim_utf8_tail(fitted, &length);
        trim_utf8_tail(fitted, &length);
        trim_utf8_tail(fitted, &length);
        memcpy(fitted + length, "...", 4U);
    }
    const uint8_t band_width = ui_text_width(canvas, fitted) + 8U;
    draw_header_band(canvas, 0U, 0U, band_width, height);
    canvas_set_color(canvas, ColorWhite);
    ui_draw_text(canvas, 4U, baseline, fitted);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_page_header(
    Canvas* canvas,
    const char* title,
    uint8_t max_width,
    uint8_t height,
    uint8_t baseline,
    Font font) {
    draw_page_header_raw(canvas, ui_translate(title), max_width, height, baseline, font);
}

static void draw_main_header(
    Canvas* canvas,
    const char* title,
    const SwitchMacroApp* app,
    Font font,
    uint8_t height,
    uint8_t baseline,
    bool show_battery) {
    draw_page_header(canvas, title, 104U, height, baseline, font);
    if(show_battery) {
        if(main_usb_icon_visible(app)) draw_usb_status_icon_mode(canvas, app, 104U, 0U);
        draw_battery_status_icon(canvas, 117U, 0U);
    } else if(main_usb_icon_visible(app)) {
        draw_usb_status_icon_mode(canvas, app, 116U, 0U);
    }
    canvas_set_font(canvas, FontSecondary);
}

static void draw_menu_item(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* label,
    const char* detail,
    bool selected) {
    char translated_label[64];
    char translated_detail[32] = {0};
    snprintf(translated_label, sizeof(translated_label), "%s", ui_translate(label));
    if(detail) snprintf(translated_detail, sizeof(translated_detail), "%s", ui_translate(detail));
    if(selected) canvas_draw_rbox(canvas, x, y, width, height, 2U);

    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
    const uint8_t baseline = y + height - 2U;
    ui_draw_text(canvas, x + 5U, baseline, translated_label);
    if(translated_detail[0]) {
        const uint8_t detail_width = ui_text_width(canvas, translated_detail);
        const uint8_t detail_x = x + width - detail_width - 5U;
        ui_draw_text(canvas, detail_x, baseline, translated_detail);
    }
    canvas_set_color(canvas, ColorBlack);
}

static void draw_ui_message_banner(Canvas* canvas, const char* message) {
    if(!message || !message[0]) return;
    char fitted[96];
    snprintf(fitted, sizeof(fitted), "%s", ui_translate(message));
    canvas_set_font(canvas, FontSecondary);
    size_t length = strlen(fitted);
    while(length > 0U && ui_text_width(canvas, fitted) > 120U) {
        trim_utf8_tail(fitted, &length);
    }
    canvas_draw_rbox(canvas, 0U, 54U, 128U, 10U, 2U);
    canvas_set_color(canvas, ColorWhite);
    ui_draw_text(canvas, 4U, 62U, fitted);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_menu_tab(
    Canvas* canvas,
    uint8_t x,
    uint8_t width,
    const char* label,
    bool selected) {
    const uint8_t y = 1U;
    const uint8_t height = 9U;
    if(selected) canvas_draw_rbox(canvas, x, y, width, height, 2U);

    const char* translated = ui_translate(label);
    canvas_set_font(canvas, FontSecondary);
    canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
    const uint8_t label_width = ui_text_width(canvas, translated);
    const uint8_t label_x = x + (width > label_width ? (width - label_width) / 2U : 1U);
    ui_draw_text(canvas, label_x, y + height - 2U, translated);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_trash_menu_icon(Canvas* canvas) {
    const uint8_t x = 112U;
    canvas_draw_line(canvas, x + 4U, 0U, x + 8U, 0U);
    canvas_draw_line(canvas, x + 4U, 0U, x + 4U, 2U);
    canvas_draw_line(canvas, x + 8U, 0U, x + 8U, 2U);
    canvas_draw_line(canvas, x + 1U, 3U, x + 11U, 3U);
    canvas_draw_line(canvas, x + 1U, 3U, x + 1U, 5U);
    canvas_draw_line(canvas, x + 11U, 3U, x + 11U, 5U);
    canvas_draw_line(canvas, x + 1U, 5U, x + 11U, 5U);
    canvas_draw_line(canvas, x + 3U, 6U, x + 3U, 9U);
    canvas_draw_line(canvas, x + 9U, 6U, x + 9U, 9U);
    canvas_draw_line(canvas, x + 3U, 9U, x + 4U, 10U);
    canvas_draw_line(canvas, x + 4U, 10U, x + 8U, 10U);
    canvas_draw_line(canvas, x + 9U, 9U, x + 8U, 10U);
    canvas_draw_line(canvas, x + 4U, 6U, x + 4U, 8U);
    canvas_draw_line(canvas, x + 6U, 6U, x + 6U, 8U);
    canvas_draw_line(canvas, x + 8U, 6U, x + 8U, 8U);
}

static void draw_scrollbar(
    Canvas* canvas,
    uint8_t top,
    uint8_t height,
    uint16_t total_items,
    uint16_t first_item,
    uint8_t visible_items) {
    if(total_items <= visible_items || height < 4U) return;

    const uint8_t track_x = 126U;
    for(uint8_t y = top; y < top + height; y = (uint8_t)(y + 2U)) {
        canvas_draw_dot(canvas, track_x, y);
    }

    uint16_t thumb_height_value = (uint16_t)((uint32_t)height * visible_items / total_items);
    if(thumb_height_value < 4U) thumb_height_value = 4U;
    if(thumb_height_value > height) thumb_height_value = height;
    const uint8_t thumb_height = (uint8_t)thumb_height_value;
    const uint16_t max_first = total_items - visible_items;
    if(first_item > max_first) first_item = max_first;
    const uint8_t travel = height - thumb_height;
    const uint8_t thumb_top = (uint8_t)(top + (max_first ? (uint32_t)travel * first_item / max_first : 0U));
    canvas_draw_box(canvas, track_x - 1U, thumb_top, 3U, thumb_height);
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
    if(pressed) {
        canvas_draw_rbox(canvas, x, y, width, height, 3U);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_rbox(canvas, x, y, width, height, 3U);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, x, y, width, height, 3U);
    }
    canvas_set_color(canvas, pressed ? ColorWhite : ColorBlack);
    canvas_set_font(canvas, FontKeyboard);
    const uint8_t label_width = ui_text_width(canvas, label);
    const uint8_t label_x = x + (width > label_width ? (width - label_width) / 2U : 0U);
    ui_draw_text(canvas, label_x, y + height - 1U, label);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_controller_trigger(
    Canvas* canvas,
    uint8_t x,
    uint8_t y,
    uint8_t width,
    uint8_t height,
    const char* label,
    bool pressed) {
    if(pressed) {
        canvas_draw_rbox(canvas, x, y, width, height, 3U);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_rbox(canvas, x, y, width, height, 3U);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_line(canvas, x + 1U, y + height - 1U, x + 1U, y + 3U);
        canvas_draw_line(canvas, x + 1U, y + 3U, x + 3U, y + 1U);
        canvas_draw_line(canvas, x + 3U, y + 1U, x + width - 3U, y + 1U);
        canvas_draw_line(canvas, x + width - 3U, y + 1U, x + width - 1U, y + 3U);
        canvas_draw_line(canvas, x + width - 1U, y + 3U, x + width - 1U, y + height - 1U);
        canvas_draw_line(canvas, x + 1U, y + height - 1U, x + width - 1U, y + height - 1U);
    }

    canvas_set_color(canvas, pressed ? ColorWhite : ColorBlack);
    canvas_set_font(canvas, FontKeyboard);
    const uint8_t label_width = ui_text_width(canvas, label);
    const uint8_t label_x = x + (width > label_width ? (width - label_width) / 2U : 0U);
    ui_draw_text(canvas, label_x, y + height - 1U, label);
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
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_disc(canvas, center_x, center_y, radius);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_circle(canvas, center_x, center_y, radius);
        canvas_set_color(canvas, ColorBlack);
    }
    canvas_set_font(canvas, FontKeyboard);
    const uint8_t label_width = ui_text_width(canvas, label);
    const uint8_t label_x = center_x - label_width / 2U;
    ui_draw_text(canvas, label_x, center_y + 3U, label);
    canvas_set_color(canvas, ColorBlack);
}

static void draw_controller_face_button(
    Canvas* canvas,
    const SwitchMacroApp* app,
    uint8_t center_x,
    uint8_t center_y,
    FaceButtonPosition position,
    bool pressed) {
    const uint8_t radius = 5U;
    if(pressed) {
        canvas_draw_disc(canvas, center_x, center_y, radius);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_disc(canvas, center_x, center_y, radius);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_circle(canvas, center_x, center_y, radius);
    }

    if(app->settings.controller_layout == ControllerLayoutPlayStation) {
        const Color symbol_color = pressed ? ColorWhite : ColorBlack;
        canvas_set_color(canvas, symbol_color);
        if(position == FaceButtonTop) {
            canvas_draw_line(canvas, center_x, center_y - 2U, center_x - 2U, center_y + 2U);
            canvas_draw_line(canvas, center_x - 2U, center_y + 2U, center_x + 2U, center_y + 2U);
            canvas_draw_line(canvas, center_x + 2U, center_y + 2U, center_x, center_y - 2U);
        } else if(position == FaceButtonLeft) {
            canvas_draw_frame(canvas, center_x - 2U, center_y - 2U, 5U, 5U);
        } else if(position == FaceButtonRight) {
            canvas_draw_circle(canvas, center_x, center_y, 2U);
        } else {
            canvas_draw_line(canvas, center_x - 2U, center_y - 2U, center_x + 2U, center_y + 2U);
            canvas_draw_line(canvas, center_x + 2U, center_y - 2U, center_x - 2U, center_y + 2U);
        }
    } else {
        static const char* const switch_labels[] = {"X", "Y", "A", "B"};
        static const char* const xbox_labels[] = {"Y", "X", "B", "A"};
        const char* label = app->settings.controller_layout == ControllerLayoutXbox ?
                                xbox_labels[position] : switch_labels[position];
        canvas_set_font(canvas, FontKeyboard);
        const uint8_t label_width = ui_text_width(canvas, label);
        const uint8_t label_x = center_x - label_width / 2U;
        ui_draw_text(canvas, label_x, center_y + 3U, label);
    }
    canvas_set_color(canvas, ColorBlack);
}

static void draw_controller_capture_button(Canvas* canvas, uint8_t center_x, uint8_t center_y, bool pressed) {
    const uint8_t x = center_x - 4U;
    const uint8_t y = center_y - 4U;
    if(pressed) {
        canvas_draw_box(canvas, x, y, 9U, 9U);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_disc(canvas, center_x, center_y, 2U);
    } else {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x, y, 9U, 9U);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, x, y, 9U, 9U);
        canvas_draw_circle(canvas, center_x, center_y, 2U);
    }
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

static void draw_live_controller_icon(
    Canvas* canvas,
    const SwitchMacroApp* app,
    const SwitchReport* report) {
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

    draw_controller_pill(canvas, 21U, 5U, 13U, 7U, "L", live_report_has_button(report, SWITCH_BUTTON_L));
    draw_controller_trigger(canvas, 36U, 5U, 16U, 7U, "ZL", live_report_has_button(report, SWITCH_BUTTON_ZL));
    draw_controller_trigger(canvas, 76U, 5U, 16U, 7U, "ZR", live_report_has_button(report, SWITCH_BUTTON_ZR));
    draw_controller_pill(canvas, 94U, 9U, 12U, 7U, "R", live_report_has_button(report, SWITCH_BUTTON_R));

    draw_live_stick(canvas, 27, 23, report->lx, report->ly, false);
    draw_controller_pill(canvas, 8U, 35U, 18U, 8U, "L3", live_report_has_button(report, SWITCH_BUTTON_L3));
    draw_controller_dpad(canvas, report);
    draw_live_stick(canvas, 82, 37, report->rx, report->ry, false);
    draw_controller_pill(canvas, 74U, 52U, 17U, 8U, "R3", live_report_has_button(report, SWITCH_BUTTON_R3));

    draw_controller_round_button(canvas, 64, 15, 5U, "H", live_report_has_button(report, SWITCH_BUTTON_HOME));
    draw_controller_round_button(canvas, 57, 23, 4U, "-", live_report_has_button(report, SWITCH_BUTTON_MINUS));
    draw_controller_round_button(canvas, 72, 23, 4U, "+", live_report_has_button(report, SWITCH_BUTTON_PLUS));
    draw_controller_capture_button(
        canvas,
        64U,
        30U,
        live_report_has_button(report, SWITCH_BUTTON_CAPTURE));

    draw_controller_face_button(
        canvas,
        app,
        109U,
        22U,
        FaceButtonTop,
        live_report_has_button(report, SWITCH_BUTTON_X));
    draw_controller_face_button(
        canvas,
        app,
        99U,
        33U,
        FaceButtonLeft,
        live_report_has_button(report, SWITCH_BUTTON_Y));
    draw_controller_face_button(
        canvas,
        app,
        119U,
        33U,
        FaceButtonRight,
        live_report_has_button(report, SWITCH_BUTTON_A));
    draw_controller_face_button(
        canvas,
        app,
        109U,
        44U,
        FaceButtonBottom,
        live_report_has_button(report, SWITCH_BUTTON_B));
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
    } else if(selection == SimulatorControlCapture) {
        canvas_draw_rframe(canvas, control->x - 5U, control->y - 5U, 10U, 10U, 1U);
    } else if(selection >= SimulatorControlDpadUp && selection <= SimulatorControlDpadRight) {
        canvas_draw_circle(canvas, control->x, control->y, 4U);
    } else {
        canvas_draw_circle(canvas, control->x, control->y, 7U);
    }
}

static void draw_simulator_ui(Canvas* canvas, const SwitchMacroApp* app) {
    draw_live_controller_icon(canvas, app, &app->simulator_report);
    draw_simulator_focus(canvas, app->simulator_selection, app->simulator_stick_active);
    draw_connection_status_icons(canvas, app);
    if((!app->connected && !app->dev_fake_usb) || app->simulator_error) {
        canvas_draw_box(canvas, 0, 54, 128, 10);
        canvas_set_color(canvas, ColorWhite);
        UI_STR(canvas, 35, 62, app->connected ? "USB SEND ERROR" : "USB DISCONNECTED");
        canvas_set_color(canvas, ColorBlack);
    }
}

static const char* macro_hat_label(uint8_t hat);

static void get_live_display_state(
    const SwitchMacroApp* app,
    SwitchReport* report,
    LiveRecentInput* recent,
    uint8_t* recent_count) {
    *report = neutral_gamepad();
    if(recent_count) *recent_count = 0U;
    if(!app->live_record_mutex ||
       furi_mutex_acquire(app->live_record_mutex, 0U) != FuriStatusOk) {
        return;
    }
    if(app->live_latest_valid) *report = app->live_latest_state;
    if(recent_count) {
        *recent_count = app->live_recent_count;
        if(*recent_count > LIVE_RECENT_INPUT_COUNT) *recent_count = LIVE_RECENT_INPUT_COUNT;
        if(recent && *recent_count) {
            memcpy(recent, app->live_recent_inputs, *recent_count * sizeof(*recent));
        }
    }
    furi_mutex_release(app->live_record_mutex);
}

static void draw_live_controller(Canvas* canvas, const SwitchMacroApp* app) {
    SwitchReport report;
    get_live_display_state(app, &report, NULL, NULL);
    draw_live_controller_icon(canvas, app, &report);
    draw_connection_status_icons(canvas, app);
}

static void draw_live_text_fit(Canvas* canvas, uint8_t x, uint8_t y, const char* text, uint8_t max_width) {
    char fitted[72];
    canvas_set_font(canvas, FontSecondary);
    snprintf(fitted, sizeof(fitted), "%s", ui_translate(text));
    const size_t original_length = strlen(fitted);
    size_t length = original_length;
    while(length && ui_text_width(canvas, fitted) > max_width) {
        trim_utf8_tail(fitted, &length);
    }
    if(length < original_length && length > 3U) {
        trim_utf8_tail(fitted, &length);
        trim_utf8_tail(fitted, &length);
        trim_utf8_tail(fitted, &length);
        memcpy(fitted + length, "...", 4U);
    }
    ui_draw_text(canvas, x, y, fitted);
}

static SwitchReport macro_step_display_report(const SwitchMacroApp* app, uint16_t index) {
    if(!app->steps || !app->step_count) return neutral_gamepad();
    if(index >= app->step_count) index = 0U;
    const MacroStep* step = &app->steps[index];
    const GameProfile* profile = active_profile_const(app);
    const SwitchReport report = {
        .buttons = step->buttons,
        .hat = step->hat,
        .lx = transform_axis(step->lx, profile->left_deadzone, profile->left_gain_percent),
        .ly = transform_axis(step->ly, profile->left_deadzone, profile->left_gain_percent),
        .rx = transform_axis(step->rx, profile->right_deadzone, profile->right_gain_percent),
        .ry = transform_axis(step->ry, profile->right_deadzone, profile->right_gain_percent),
        .vendor = 0U,
    };
    return report;
}

static void draw_macro_playback_state_icon(Canvas* canvas, bool playing) {
    if(playing) {
        canvas_draw_line(canvas, 2U, 1U, 2U, 9U);
        canvas_draw_line(canvas, 2U, 1U, 9U, 5U);
        canvas_draw_line(canvas, 9U, 5U, 2U, 9U);
        canvas_draw_line(canvas, 4U, 3U, 4U, 7U);
        canvas_draw_line(canvas, 6U, 4U, 6U, 6U);
    } else {
        canvas_draw_box(canvas, 3U, 2U, 2U, 7U);
        canvas_draw_box(canvas, 7U, 2U, 2U, 7U);
    }
}

static void draw_macro_controller_frame(
    Canvas* canvas,
    const SwitchMacroApp* app,
    uint16_t step_index,
    uint8_t progress) {
    if(step_index >= app->step_count && app->step_count > 0U) step_index = app->step_count - 1U;
    const SwitchReport report = macro_step_display_report(app, step_index);
    draw_live_controller_icon(canvas, app, &report);
    draw_macro_playback_state_icon(canvas, app->state == RunSending && !app->macro_paused);
    draw_connection_status_icons(canvas, app);
    if(progress > 128U) progress = 128U;
    if(app->step_count > 0U && progress == 0U) progress = 1U;
    if(progress > 0U) canvas_draw_line(canvas, 0U, 63U, (uint8_t)(progress - 1U), 63U);
}

static void draw_macro_playback_controller(Canvas* canvas, const SwitchMacroApp* app) {
    uint32_t elapsed_ms = app->macro_elapsed_ms;
    if(app->macro_playback_total_ms > 0U && elapsed_ms >= app->macro_playback_total_ms &&
       (app->macro_loop || app->macro_repeat_count > 1U)) {
        elapsed_ms %= app->macro_playback_total_ms;
    }
    uint64_t progress_value = app->macro_playback_total_ms > 0U ?
                                  (uint64_t)elapsed_ms * 128U / app->macro_playback_total_ms :
                                  0U;
    if(progress_value > 128U) progress_value = 128U;
    const uint8_t progress = (uint8_t)progress_value;
    draw_macro_controller_frame(canvas, app, app->playback_step, progress);
}

static void draw_macro_view_controller(Canvas* canvas, const SwitchMacroApp* app) {
    const uint16_t step_index = app->macro_view_step < app->step_count ? app->macro_view_step : 0U;
    const uint8_t progress = app->step_count > 0U ?
                                 (uint8_t)(((uint32_t)(step_index + 1U) * 128U) / app->step_count) :
                                 0U;
    draw_macro_controller_frame(canvas, app, step_index, progress);
}

static void advance_backup_preview(SwitchMacroApp* app) {
    if(app->state != RunReady || app->macro_ui_screen != MacroUiBackupPreview ||
       !app->backup_loaded || !app->backup_preview_playing || !app->steps || app->step_count == 0U) {
        return;
    }
    const uint16_t index = app->backup_preview_step < app->step_count ? app->backup_preview_step : 0U;
    uint32_t duration_ms = app->steps[index].duration_ms;
    if(duration_ms == 0U) duration_ms = 1U;
    const uint32_t now = furi_get_tick();
    if(now - app->backup_preview_step_started >= furi_ms_to_ticks(duration_ms)) {
        app->backup_preview_step = (uint16_t)((index + 1U) % app->step_count);
        app->backup_preview_step_started = now;
    }
}

static void draw_rename_ui(Canvas* canvas, const SwitchMacroApp* app);

static void draw_live_recorder(Canvas* canvas, const SwitchMacroApp* app) {
    if(app->live_ui_screen == LiveUiRenameRecording) {
        draw_rename_ui(canvas, app);
        return;
    }
    char line[28];
    char elapsed[12];
    const uint32_t record_elapsed = app->live_recording && !app->live_record_paused ?
                                        app->live_record_elapsed_ms +
                                            furi_get_tick() - app->live_record_started_tick :
                                        app->live_record_elapsed_ms;
    format_macro_duration(record_elapsed, elapsed, sizeof(elapsed));

    if(app->live_recording) {
        snprintf(line, sizeof(line), "%s", app->live_record_paused ? "RECORDING PAUSED" : "RECORDING INPUT");
    } else if(app->live_record_full) {
        snprintf(line, sizeof(line), "STORAGE LIMIT REACHED");
    } else if(app->live_record_limit_reached) {
        snprintf(line, sizeof(line), "TIME LIMIT REACHED");
    } else if(app->live_record_pending_text) {
        snprintf(line, sizeof(line), "RECORDING READY TO SAVE");
    } else if(app->live_record_saved) {
        snprintf(line, sizeof(line), "SAVED %.18s", app->live_record_saved_name);
    } else {
        snprintf(line, sizeof(line), "READY TO RECORD");
    }

    draw_connection_status_icons(canvas, app);
    draw_page_header(canvas, line, 98U, 10U, 9U, FontSecondary);

    canvas_draw_frame(canvas, 4, 13, 58, 21);
    canvas_draw_frame(canvas, 66, 13, 58, 21);
    UI_STR(canvas, 9, 22, "Duration");
    UI_STR(canvas, 71, 22, "Steps");
    canvas_set_font(canvas, FontPrimary);
    ui_draw_text(canvas, 9, 30, elapsed);
    snprintf(line, sizeof(line), "%u", (unsigned)app->live_record_steps);
    ui_draw_text(canvas, 71, 30, line);
    canvas_set_font(canvas, FontSecondary);

    const uint8_t visible_rows = 3U;
    const uint8_t first = app->live_recorder_selection >= visible_rows ?
                              app->live_recorder_selection - visible_rows + 1U :
                              0U;
    for(uint8_t row = 0U; row < visible_rows; row++) {
        const uint8_t index = first + row;
        if(index >= 4U) break;
        const uint8_t y = 36U + row * 9U;
        const bool selected = index == app->live_recorder_selection;
        const char* label = index == 0U ?
                                (app->live_record_pending_text ? "Save recording" :
                                 (app->live_recording || app->live_record_full || app->live_record_limit_reached ? "Stop and save" :
                                     (app->live_record_saved ? "Record again" : "Start recording"))) :
                            index == 1U ?
                                (app->live_recording && app->live_record_paused ? "Resume recording" :
                                     "Pause recording") :
                            index == 2U ? "View controller" : "Backup";
        draw_menu_item(canvas, 2U, y, 122U, 9U, label, NULL, selected);
    }
    draw_scrollbar(canvas, 36U, 27U, 4U, first, visible_rows);
    draw_ui_message_banner(canvas, app->ui_message);
}

static void draw_rename_action_icon(
    Canvas* canvas,
    const RenameKey* key,
    uint8_t frame_x,
    uint8_t baseline) {
    const uint8_t center_x = frame_x + key->width / 2U;
    if(key->action == RenameKeySymbols) {
        canvas_draw_line(canvas, center_x, baseline - 7U, center_x, baseline - 2U);
        canvas_draw_line(canvas, center_x, baseline - 7U, center_x - 3U, baseline - 4U);
        canvas_draw_line(canvas, center_x, baseline - 7U, center_x + 3U, baseline - 4U);
        canvas_draw_line(canvas, center_x - 2U, baseline - 2U, center_x + 2U, baseline - 2U);
    } else if(key->action == RenameKeyCase) {
        canvas_set_font(canvas, FontSecondary);
        const uint8_t label_width = ui_text_width(canvas, "Aa");
        const uint8_t label_x = frame_x + (key->width > label_width ? (key->width - label_width) / 2U : 0U);
        ui_draw_text(canvas, label_x, baseline - 1U, "Aa");
        canvas_set_font(canvas, FontKeyboard);
    } else if(key->action == RenameKeyBackspace) {
        const uint8_t middle_y = baseline - 5U;
        const uint8_t body_left = frame_x + 6U;
        const uint8_t body_right = frame_x + key->width - 3U;
        const uint8_t cross_x = body_left + (body_right - body_left) / 2U;
        canvas_draw_line(canvas, frame_x + 3U, middle_y, body_left, middle_y - 2U);
        canvas_draw_line(canvas, frame_x + 3U, middle_y, body_left, middle_y + 2U);
        canvas_draw_line(canvas, body_left, middle_y - 2U, body_right, middle_y - 2U);
        canvas_draw_line(canvas, body_left, middle_y + 2U, body_right, middle_y + 2U);
        canvas_draw_line(canvas, body_right, middle_y - 2U, body_right, middle_y + 2U);
        canvas_draw_line(canvas, cross_x - 1U, middle_y - 1U, cross_x + 1U, middle_y + 1U);
        canvas_draw_line(canvas, cross_x + 1U, middle_y - 1U, cross_x - 1U, middle_y + 1U);
    } else if(key->action == RenameKeySave) {
        const uint8_t x = center_x - 4U;
        const uint8_t y = baseline - 8U;
        canvas_draw_frame(canvas, x, y, 8U, 8U);
        canvas_draw_line(canvas, x + 1U, y + 3U, x + 6U, y + 3U);
        canvas_draw_frame(canvas, x + 2U, y + 5U, 4U, 2U);
    }
}

static void draw_rename_ui(Canvas* canvas, const SwitchMacroApp* app) {
    char preview[MACRO_BASENAME_MAX + 2U];
    const size_t length = strlen(app->rename_name);
    const size_t cursor = app->rename_position > length ? length : app->rename_position;
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    memcpy(preview, app->rename_name, cursor);
    preview[cursor] = '|';
    memcpy(preview + cursor + 1U, app->rename_name + cursor, length - cursor + 1U);

    draw_page_header(
        canvas,
        app->ui_message[0] ? app->ui_message :
            (app->rename_target == RenameTargetProfile ? "RENAME PROFILE" :
             app->rename_target == RenameTargetRecording ? "RENAME RECORDING" :
             app->rename_target == RenameTargetRestore ? "RENAME RESTORED MACRO" : "RENAME MACRO"),
        96U,
        10U,
        8U,
        FontSecondary);
    UI_STR(canvas, 98, 8, app->rename_symbols ? "SYM" : app->rename_lowercase ? "abc" : "ABC");
    canvas_draw_frame(canvas, 2, 11, 124, 14);
    ui_draw_text(canvas, 6, 21, preview);

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
                canvas_draw_box(canvas, frame_x, baseline - 9U, key->width, 10U);
                canvas_set_color(canvas, ColorWhite);
            } else if(key->action != RenameKeyCharacter) {
                canvas_draw_frame(canvas, frame_x, baseline - 9U, key->width, 10U);
            }

            if(key->action == RenameKeyCharacter) {
                const char value = !app->rename_symbols && app->rename_lowercase &&
                                           isalpha((unsigned char)key->value) ?
                                       (char)tolower((unsigned char)key->value) :
                                       key->value;
                canvas_draw_glyph(canvas, key->x, baseline, (unsigned char)value);
            } else if(key->action == RenameKeySymbols || key->action == RenameKeyCase ||
                      key->action == RenameKeyBackspace ||
                      key->action == RenameKeySave) {
                draw_rename_action_icon(canvas, key, frame_x, baseline);
            } else {
                const char* label = "SP";
                canvas_set_font(canvas, FontSecondary);
                const char* translated_label = ui_translate(label);
                const uint8_t label_width = ui_text_width(canvas, translated_label);
                const uint8_t label_x = frame_x + (key->width > label_width ? (key->width - label_width) / 2U : 0U);
                ui_draw_text(canvas, label_x, baseline - 2U, translated_label);
                canvas_set_font(canvas, FontKeyboard);
            }
            canvas_set_color(canvas, ColorBlack);
        }
    }
    canvas_set_font(canvas, FontSecondary);
}

static void draw_profile_settings(Canvas* canvas, const SwitchMacroApp* app) {
    const GameProfile* profile = active_profile_const(app);
    char details[7][16] = {{0}};
    char title[24];
    snprintf(title, sizeof(title), "PROFILE  %.12s", app->settings.profile_names[app->settings.active_profile]);
    draw_main_header(canvas, title, app, FontSecondary, 10U, 8U, false);
    static const char* const labels[] = {
        "Change profile",
        "Deadzone",
        "Gain",
        "Simulator step",
        "Play speed",
        "Calibrate sticks",
        "Rename profile",
    };
    snprintf(details[0], sizeof(details[0]), "%s", app->settings.profile_names[app->settings.active_profile]);
    snprintf(
        details[1],
        sizeof(details[1]),
        "L%u R%u",
        (unsigned)profile->left_deadzone,
        (unsigned)profile->right_deadzone);
    snprintf(
        details[2],
        sizeof(details[2]),
        "L%u%% R%u%%",
        (unsigned)profile->left_gain_percent,
        (unsigned)profile->right_gain_percent);
    snprintf(details[3], sizeof(details[3]), "%u", (unsigned)profile->simulator_step);
    snprintf(details[4], sizeof(details[4]), "%u%%", (unsigned)playback_speeds[profile->playback_speed_index]);

    const uint8_t visible_rows = 5U;
    const uint8_t first = app->profile_selection >= visible_rows ?
                              app->profile_selection - visible_rows + 1U :
                              0U;
    for(uint8_t row = 0U; row < visible_rows; row++) {
        const uint8_t index = first + row;
        if(index >= COUNT_OF(labels)) break;
        const uint8_t y = 12U + row * 10U;
        draw_menu_item(
            canvas,
            4U,
            y,
            120U,
            9U,
            labels[index],
            details[index][0] ? details[index] : NULL,
            index == app->profile_selection);
    }
    draw_scrollbar(canvas, 12U, 49U, COUNT_OF(labels), first, visible_rows);
}

static void draw_calibration_ui(Canvas* canvas, const SwitchMacroApp* app) {
    const GameProfile* profile = active_profile_const(app);
    char deadzone[8];
    char gain[8];
    const uint8_t stick_deadzone = app->calibration_right_stick ?
                                       profile->right_deadzone : profile->left_deadzone;
    const uint8_t stick_gain = app->calibration_right_stick ?
                                   profile->right_gain_percent : profile->left_gain_percent;
    const char* const stick_name = app->calibration_right_stick ? "Right" : "Left";
    snprintf(deadzone, sizeof(deadzone), "%u", (unsigned)stick_deadzone);
    snprintf(gain, sizeof(gain), "%u%%", (unsigned)stick_gain);
    draw_page_header(canvas, "Calibrate sticks", 120U, 10U, 8U, FontSecondary);

    static const char* const labels[] = {"Stick", "Deadzone", "Gain", "Save", "Reset"};
    const char* const values[] = {stick_name, deadzone, gain, NULL, NULL};
    for(uint8_t row = 0U; row < COUNT_OF(labels); row++) {
        draw_menu_item(
            canvas,
            4U,
            13U + row * 9U,
            96U,
            8U,
            labels[row],
            values[row],
            row == app->calibration_selection);
    }
    const uint8_t center_x = 115U;
    const uint8_t center_y = 23U;
    canvas_draw_circle(canvas, center_x, center_y, 7U);
    canvas_draw_line(canvas, center_x, center_y, center_x, center_y + 4U);
    const uint8_t knob_x = app->calibration_right_stick ? center_x + 3U : center_x - 3U;
    canvas_draw_disc(canvas, knob_x, center_y, 2U);
    canvas_set_font(canvas, FontSecondary);
    ui_draw_text(canvas, 112U, 38U, app->calibration_right_stick ? "R" : "L");
    draw_live_text_fit(canvas, 4U, 62U, "Arrows change value", 120U);
}

static void draw_macro_play_screen(Canvas* canvas, const SwitchMacroApp* app) {
    char line[40];
    char elapsed[12];
    char total[12];
    const bool playing = app->state == RunSending;
    uint32_t total_ms = playing && app->macro_playback_total_ms ?
                            app->macro_playback_total_ms :
                            app->macro_total_ms;
    uint32_t elapsed_ms = playing ? app->macro_elapsed_ms : 0U;
    if(playing && total_ms > 0U && elapsed_ms >= total_ms && (app->macro_loop || app->macro_repeat_count > 1U)) {
        elapsed_ms %= total_ms;
    }
    format_macro_duration(elapsed_ms, elapsed, sizeof(elapsed));
    format_macro_duration(total_ms, total, sizeof(total));

    draw_page_header(canvas, "Play macro", 98U, 10U, 8U, FontSecondary);
    draw_connection_status_icons(canvas, app);
    draw_macro_filename_at(canvas, 4U, app->macro_files[app->macro_index].name, 18);

    const uint16_t current_step = playing ? app->playback_step + 1U : 0U;
    snprintf(line, sizeof(line), "STEP %u/%u", (unsigned)current_step, (unsigned)app->step_count);
    draw_live_text_fit(canvas, 4U, 26U, line, 120U);
    snprintf(line, sizeof(line), "%s / %s", elapsed, total);
    draw_live_text_fit(canvas, 72U, 26U, line, 52U);

    uint32_t progress_width = total_ms > 0U ? (uint32_t)((uint64_t)elapsed_ms * 118U / total_ms) : 0U;
    if(progress_width > 118U) progress_width = 118U;
    canvas_draw_frame(canvas, 4U, 30U, 120U, 5U);
    if(progress_width > 0U) canvas_draw_box(canvas, 5U, 31U, progress_width, 3U);

    draw_menu_item(
        canvas,
        4U,
        39U,
        120U,
        9U,
        !playing ? "Start macro" : (app->macro_paused ? "Resume macro" : "Pause macro"),
        NULL,
        app->macro_play_selection == 0U);
    draw_menu_item(
        canvas,
        4U,
        50U,
        120U,
        9U,
        "View controller",
        NULL,
        app->macro_play_selection == 1U);
}

static void draw_macro_preview(Canvas* canvas, const SwitchMacroApp* app);
static void draw_macro_editor(Canvas* canvas, const SwitchMacroApp* app);
static void draw_macro_profile_assignment(Canvas* canvas, const SwitchMacroApp* app);

static void draw_backup_list(Canvas* canvas, const SwitchMacroApp* app) {
    draw_page_header(canvas, "Backup", 98U, 10U, 8U, FontSecondary);
    draw_connection_status_icons(canvas, app);
    if(app->backup_count == 0U) {
        canvas_draw_frame(canvas, 5U, 17U, 118U, 26U);
        draw_live_text_fit(canvas, 12U, 33U, "No backups yet", 104U);
        return;
    }

    const uint8_t visible = BACKUP_LIST_VISIBLE;
    const uint8_t first = app->backup_offset;
    for(uint8_t row = 0U; row < visible; row++) {
        const uint8_t index = first + row;
        if(index >= app->backup_count) break;
        const uint8_t y = 12U + row * 15U;
        const bool selected = index == app->backup_index;
        if(selected) canvas_draw_rbox(canvas, 3U, y, 122U, 14U, 2U);
        canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
        draw_live_text_fit(canvas, 6U, y + 6U, app->backups[index].title, 112U);
        draw_live_text_fit(canvas, 6U, y + 13U, app->backups[index].date_time, 112U);
        canvas_set_color(canvas, ColorBlack);
    }
    draw_scrollbar(canvas, 12U, 45U, app->backup_count, app->backup_offset, visible);
}

static void draw_backup_details(Canvas* canvas, const SwitchMacroApp* app) {
    if(app->backup_index >= app->backup_count) {
        draw_page_header(canvas, "Backup", 98U, 10U, 8U, FontSecondary);
        draw_connection_status_icons(canvas, app);
        draw_live_text_fit(canvas, 8U, 31U, "Backup no longer available", 112U);
        return;
    }

    const MacroBackup* backup = &app->backups[app->backup_index];
    draw_page_header_raw(canvas, backup->title, 98U, 10U, 8U, FontSecondary);
    draw_connection_status_icons(canvas, app);
    char details[28];
    draw_live_text_fit(canvas, 5U, 17U, backup->date_time, 118U);
    snprintf(details, sizeof(details), "Steps %u  ·  Time", (unsigned)backup->step_count);
    draw_live_text_fit(canvas, 5U, 25U, details, 118U);
    char duration[12];
    format_macro_duration(backup->duration_ms, duration, sizeof(duration));
    draw_live_text_fit(canvas, 83U, 25U, duration, 38U);

    static const char* const actions[] = {
        "View controller", "Play on Switch", "Restore", "Rename & restore", "Delete backup", "Back"};
    for(uint8_t row = 0U; row < BACKUP_ACTION_VISIBLE; row++) {
        const uint8_t index = app->backup_action_offset + row;
        if(index >= COUNT_OF(actions)) break;
        const uint8_t y = 33U + row * 8U;
        draw_menu_item(
            canvas,
            3U,
            y,
            122U,
            8U,
            actions[index],
            NULL,
            index == app->backup_action_index);
    }
    draw_scrollbar(
        canvas,
        33U,
        31U,
        COUNT_OF(actions),
        app->backup_action_offset,
        BACKUP_ACTION_VISIBLE);
}

static void draw_backup_preview(Canvas* canvas, const SwitchMacroApp* app) {
    if(!app->backup_loaded || !app->steps || app->step_count == 0U) {
        draw_connection_status_icons(canvas, app);
        draw_ui_message_banner(canvas, "Backup unavailable");
        return;
    }
    const uint16_t index = app->backup_preview_step < app->step_count ? app->backup_preview_step : 0U;
    const SwitchReport report = macro_step_display_report(app, index);
    draw_live_controller_icon(canvas, app, &report);
    draw_macro_playback_state_icon(canvas, app->backup_preview_playing);
    draw_connection_status_icons(canvas, app);
    const uint8_t progress = (uint8_t)(((uint32_t)(index + 1U) * 128U) / app->step_count);
    if(progress > 0U) canvas_draw_line(canvas, 0U, 63U, progress > 128U ? 127U : progress - 1U, 63U);
}

static void draw_backup_conflict(Canvas* canvas, const SwitchMacroApp* app) {
    draw_page_header(canvas, "Name already exists", 98U, 10U, 8U, FontSecondary);
    draw_connection_status_icons(canvas, app);
    canvas_draw_line(canvas, 63U, 13U, 63U, 35U);
    draw_live_text_fit(canvas, 3U, 18U, app->backup_restore_name, 58U);
    draw_live_text_fit(canvas, 66U, 18U, app->backup_index < app->backup_count ?
                                            app->backups[app->backup_index].title : "Backup", 58U);
    UI_STR(canvas, 5U, 25U, "EXISTING");
    UI_STR(canvas, 69U, 25U, "IN BACKUP");
    char details[24];
    char duration[12];
    format_macro_duration(app->backup_existing_total_ms, duration, sizeof(duration));
    snprintf(details, sizeof(details), "S%u T%s", (unsigned)app->backup_existing_steps, duration);
    draw_live_text_fit(canvas, 3U, 32U, details, 58U);
    format_macro_duration(app->backup_index < app->backup_count ?
                              app->backups[app->backup_index].duration_ms : 0U,
                          duration,
                          sizeof(duration));
    snprintf(details, sizeof(details), "S%u T%s", app->backup_index < app->backup_count ?
                                                        (unsigned)app->backups[app->backup_index].step_count : 0U,
             duration);
    draw_live_text_fit(canvas, 66U, 32U, details, 58U);
    static const char* const choices[] = {"Overwrite", "Rename", "Add _2", "Cancel"};
    for(uint8_t i = 0U; i < COUNT_OF(choices); i++) {
        const uint8_t y = 37U + i * 7U;
        draw_menu_item(canvas, 3U, y, 122U, 7U, choices[i], NULL, i == app->backup_conflict_selection);
    }
}

static void draw_backup_delete_confirm(Canvas* canvas, const SwitchMacroApp* app) {
    draw_page_header(canvas, "Delete backup?", 98U, 10U, 8U, FontSecondary);
    draw_connection_status_icons(canvas, app);
    if(app->backup_index < app->backup_count) {
        draw_live_text_fit(canvas, 7U, 24U, app->backups[app->backup_index].title, 114U);
    }
    draw_menu_item(canvas, 8U, 34U, 112U, 9U, "Delete backup", NULL, app->backup_conflict_selection == 0U);
    draw_menu_item(canvas, 8U, 45U, 112U, 9U, "Cancel", NULL, app->backup_conflict_selection == 1U);
}

static void draw_info_mascot(Canvas* canvas, const SwitchMacroApp* app);

static void draw_macro_ui(Canvas* canvas, const SwitchMacroApp* app) {
    char line[40];
    char duration[12];
    if(app->macro_ui_screen == MacroUiHome) {
        draw_main_header(canvas, "SWITCH  MACRO", app, FontSecondary, 10U, 8U, true);

        static const char* const labels[] = {
            "Macros", "Live controller", "Simulator", "Game profiles", "Options"};
        for(uint8_t i = 0U; i < COUNT_OF(labels); i++) {
            const uint8_t y = 12U + i * 10U;
            const bool selected = i == app->home_index;
            const char* detail = NULL;
            if(i == 0U) {
                snprintf(line, sizeof(line), "%u Files", (unsigned)app->macro_total_count);
                detail = line;
            } else if(i == 1U) {
                detail = "PC";
            } else if(i == 2U) {
                detail = "USB";
            }
            draw_menu_item(canvas, 4U, y, 120U, 9U, labels[i], detail, selected);
        }
    } else if(app->macro_ui_screen == MacroUiExitConfirm) {
        draw_page_header(canvas, "Exit?", 98U, 10U, 9U, FontSecondary);
        draw_connection_status_icons(canvas, app);
        UI_STR(canvas, 16U, 25U, "Do you want to exit?");
        draw_menu_item(canvas, 8U, 34U, 112U, 10U, "Stay", NULL, app->exit_selection == 0U);
        draw_menu_item(canvas, 8U, 46U, 112U, 10U, "Exit", NULL, app->exit_selection == 1U);
    } else if(app->macro_ui_screen == MacroUiList) {
        static const char* const views[] = {"ALL", "FAVS", "RECENT"};
        uint8_t view_x[COUNT_OF(views)];
        uint8_t view_w[COUNT_OF(views)];
        uint16_t tabs_width = 0U;
        canvas_set_font(canvas, FontSecondary);
        for(uint8_t i = 0U; i < COUNT_OF(views); i++) {
            const uint8_t label_width = ui_text_width(canvas, ui_translate(views[i]));
            view_w[i] = label_width + 8U;
            if(view_w[i] < 24U) view_w[i] = 24U;
            tabs_width += view_w[i];
        }
        const uint8_t tab_gap = 4U;
        tabs_width += (COUNT_OF(views) - 1U) * tab_gap;
        uint8_t tab_x = 0U;
        for(uint8_t i = 0U; i < COUNT_OF(views); i++) {
            view_x[i] = tab_x;
            draw_menu_tab(canvas, view_x[i], view_w[i], views[i], i == app->macro_list_mode);
            tab_x = (uint8_t)(tab_x + view_w[i] + tab_gap);
        }
        draw_trash_menu_icon(canvas);
        if(app->macro_count == 0U) {
            canvas_draw_frame(canvas, 5, 13, 118, 30);
            canvas_set_font(canvas, FontPrimary);
            UI_STR(canvas, 22, 27, app->macro_list_mode == MacroListFavorites ?
                                      "NO FAVORITES" :
                                  app->macro_list_mode == MacroListRecent ? "NO RECENT MACROS" :
                                                                            "NO MACROS");
            canvas_set_font(canvas, FontSecondary);
            UI_STR(canvas, 24, 38, app->macro_list_mode == MacroListAll ?
                                      "Add .txt files to SD" :
                                      "Change tab with left/right");
        } else {
            const uint8_t visible_count = app->macro_count - app->macro_list_offset < MACRO_LIST_VISIBLE ?
                                              app->macro_count - app->macro_list_offset :
                                              MACRO_LIST_VISIBLE;
            for(uint8_t row = 0U; row < visible_count; row++) {
                const uint8_t index = app->macro_list_offset + row;
                const uint8_t y = 12U + row * 10U;
                const bool selected = index == app->macro_index;
                if(selected) canvas_draw_rbox(canvas, 3U, y, 122U, 9U, 2U);
                canvas_set_color(canvas, selected ? ColorWhite : ColorBlack);
                if(macro_is_favorite(app, app->macro_files[index].name)) {
                    ui_draw_text(canvas, 5U, y + 7U, "*");
                }
                draw_macro_filename_at(canvas, 11U, app->macro_files[index].name, y + 7U);
                canvas_set_color(canvas, ColorBlack);
            }
            draw_scrollbar(
                canvas,
                12U,
                45U,
                app->macro_count,
                app->macro_list_offset,
                visible_count);

            if(!app->ui_message[0] && app->load_error == MacroLoadOk) {
                format_macro_duration(app->macro_total_ms, duration, sizeof(duration));
                snprintf(line, sizeof(line), "Steps %u  Time %s", (unsigned)app->step_count, duration);
                UI_STR(canvas, 5, 61, line);
            }
        }
    } else if(app->macro_ui_screen == MacroUiDetails) {
        char title[MACRO_FILENAME_MAX];
        snprintf(title, sizeof(title), "%s", app->macro_files[app->macro_index].name);
        const size_t title_length = strlen(title);
        if(title_length >= 4U && strcmp(title + title_length - 4U, ".txt") == 0) {
            title[title_length - 4U] = '\0';
        }
        canvas_set_font(canvas, FontSecondary);
        draw_page_header_raw(canvas, title, 98U, 9U, 8U, FontSecondary);
        draw_connection_status_icons(canvas, app);
        format_macro_duration(app->macro_total_ms, duration, sizeof(duration));
        snprintf(
            line,
            sizeof(line),
            "Steps %u  ·  Time %s",
            (unsigned)app->step_count,
            duration);
        UI_STR(canvas, 5U, 18U, line);

        const char* loop_detail = app->macro_loop ? "< On >" : "< Off >";
        snprintf(line, sizeof(line), "< %u >", (unsigned)app->macro_repeat_count);
        draw_menu_item(
            canvas,
            3U,
            21U,
            122U,
            9U,
            "Loop",
            loop_detail,
            app->macro_detail_selection == 0U);
        draw_menu_item(
            canvas,
            3U,
            31U,
            122U,
            9U,
            "Repeats",
            line,
            app->macro_detail_selection == 1U);
        draw_menu_item(canvas, 3U, 41U, 122U, 9U, "Play macro", NULL, app->macro_detail_selection == 2U);
        draw_menu_item(
            canvas,
            3U,
            51U,
            122U,
            9U,
            "Manage macro",
            NULL,
            app->macro_detail_selection == 3U);
    } else if(app->macro_ui_screen == MacroUiOptions) {
        draw_main_header(canvas, "OPTIONS", app, FontSecondary, 10U, 8U, false);
        char recording_limit[12];
        static const char* const language_names[] = {"English", "Espanol"};
        const char* const layout_names[] = {"Switch", "PS5", "Xbox"};
        const char* const labels[] = {
            "Language", "Button layout", "Keep screen on", "Sound", "Rec time limit",
            "Dev Fake USB", "Logs", "Info"};
        snprintf(
            recording_limit,
            sizeof(recording_limit),
            "%u m",
            (unsigned)recording_limit_minutes[app->settings.recording_limit_index]);
        const char* values[] = {
            app->settings.language < COUNT_OF(language_names) ?
                language_names[app->settings.language] :
                language_names[UiLanguageEnglish],
            app->settings.controller_layout < COUNT_OF(layout_names) ?
                layout_names[app->settings.controller_layout] :
                layout_names[ControllerLayoutSwitch],
            app->settings.keep_screen_on ? "On" : "Off",
            app->settings.sound_enabled ? "On" : "Off",
            recording_limit,
            app->dev_fake_usb ? "On" : "Off",
            app->error_logging_enabled ? "On" : "Off",
            NULL,
        };
        const uint8_t visible_rows = 5U;
        const uint8_t first = app->options_selection >= visible_rows ?
                                  app->options_selection - visible_rows + 1U :
                                  0U;
        for(uint8_t row = 0U; row < visible_rows; row++) {
            const uint8_t i = first + row;
            if(i >= COUNT_OF(labels)) break;
            draw_menu_item(
                canvas,
                4U,
                12U + row * 10U,
                120U,
                9U,
                labels[i],
                values[i],
                app->options_selection == i);
        }
        draw_scrollbar(canvas, 12U, 49U, COUNT_OF(labels), first, visible_rows);
    } else if(app->macro_ui_screen == MacroUiInfo) {
        char version_line[32];
        static const char* const info_lines[] = {
            NULL,
            "Created by Syzygy",
            "PC software on GitHub",
            "GitHub:",
            "github.com/Syzygye/",
            "Switch-macro",
        };
        snprintf(version_line, sizeof(version_line), "Switch Macro v%s", SWITCH_MACRO_VERSION);
        draw_page_header(canvas, "Info", 98U, 10U, 8U, FontSecondary);
        draw_connection_status_icons(canvas, app);
        canvas_set_font(canvas, FontSecondary);
        for(uint8_t row = 0U; row < INFO_VISIBLE_ROWS; row++) {
            const uint8_t line_index = app->info_scroll + row;
            if(line_index >= COUNT_OF(info_lines)) break;
            const char* line = line_index == 0U ? version_line : info_lines[line_index];
            draw_live_text_fit(canvas, 4U, 19U + row * 10U, line, 118U);
        }
        draw_scrollbar(canvas, 18U, INFO_SCROLLBAR_HEIGHT, COUNT_OF(info_lines), app->info_scroll, INFO_VISIBLE_ROWS);
        draw_info_mascot(canvas, app);
    } else if(app->macro_ui_screen == MacroUiPlay) {
        draw_macro_play_screen(canvas, app);
    } else if(app->macro_ui_screen == MacroUiViewController) {
        draw_macro_view_controller(canvas, app);
    } else if(app->macro_ui_screen == MacroUiActions) {
        static const char* actions[] = {
            "Preview steps", "Edit macro", "Assign profile", "Add favorite", "Rename file", "Delete file"};
        draw_page_header(canvas, "Manage macro", 98U, 10U, 8U, FontSecondary);
        canvas_set_font(canvas, FontSecondary);
        draw_connection_status_icons(canvas, app);
        for(uint8_t i = 0U; i < COUNT_OF(actions); i++) {
            const uint8_t y = 11U + (uint8_t)(i * 9U);
            const bool selected = i == app->macro_action_index;
            const char* label = actions[i];
            if(i == 3U && macro_is_favorite(app, app->macro_files[app->macro_index].name)) {
                label = "Remove favorite";
            }
            draw_menu_item(canvas, 3U, y, 122U, 8U, label, NULL, selected);
        }
    } else if(app->macro_ui_screen == MacroUiRename) {
        draw_rename_ui(canvas, app);
    } else if(app->macro_ui_screen == MacroUiDeleteConfirm) {
        canvas_draw_frame(canvas, 4, 10, 120, 28);
        canvas_set_font(canvas, FontPrimary);
        UI_STR(canvas, 12, 24, "Move to Backup?");
        canvas_set_font(canvas, FontSecondary);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 35);
        canvas_draw_rbox(canvas, 2U, 43U, 124U, 10U, 2U);
        canvas_set_color(canvas, ColorWhite);
        UI_STR(canvas, 8, 51, "MOVE TO BACKUP");
        canvas_set_color(canvas, ColorBlack);
    } else if(app->macro_ui_screen == MacroUiPreview) {
        draw_macro_preview(canvas, app);
    } else if(app->macro_ui_screen == MacroUiEditor) {
        draw_macro_editor(canvas, app);
    } else if(app->macro_ui_screen == MacroUiAssignProfile) {
        draw_macro_profile_assignment(canvas, app);
    } else if(app->macro_ui_screen == MacroUiSimulator) {
        draw_simulator_ui(canvas, app);
    } else if(app->macro_ui_screen == MacroUiProfiles) {
        draw_profile_settings(canvas, app);
    } else if(app->macro_ui_screen == MacroUiCalibration) {
        draw_calibration_ui(canvas, app);
    } else if(app->macro_ui_screen == MacroUiBackupList) {
        draw_backup_list(canvas, app);
    } else if(app->macro_ui_screen == MacroUiBackupDetails) {
        draw_backup_details(canvas, app);
    } else if(app->macro_ui_screen == MacroUiBackupPreview) {
        draw_backup_preview(canvas, app);
    } else if(app->macro_ui_screen == MacroUiBackupConflict) {
        draw_backup_conflict(canvas, app);
    } else if(app->macro_ui_screen == MacroUiBackupDeleteConfirm) {
        draw_backup_delete_confirm(canvas, app);
    }
    draw_ui_message_banner(canvas, app->ui_message);
}

static const char* macro_hat_label(uint8_t hat) {
    switch(hat) {
    case 0x00: return "UP";
    case 0x01: return "UP RIGHT";
    case 0x02: return "RIGHT";
    case 0x03: return "DOWN RIGHT";
    case 0x04: return "DOWN";
    case 0x05: return "DOWN LEFT";
    case 0x06: return "LEFT";
    case 0x07: return "UP LEFT";
    default: return "NONE";
    }
}

static bool macro_step_is_neutral(const MacroStep* step) {
    return step->buttons == 0U && step->hat == SWITCH_HAT_NONE && step->lx == 0x80U &&
           step->ly == 0x80U && step->rx == 0x80U && step->ry == 0x80U;
}

static void format_step_buttons(const MacroStep* step, char* output, size_t capacity) {
    size_t used = 0U;
    output[0] = '\0';
    for(size_t i = 0U; i < COUNT_OF(macro_edit_buttons); i++) {
        if((step->buttons & macro_edit_buttons[i].mask) == 0U) continue;
        const int written = snprintf(
            output + used,
            capacity - used,
            "%s%s",
            used ? "+" : "",
            macro_edit_buttons[i].name);
        if(written < 0 || (size_t)written >= capacity - used) {
            if(capacity > 4U) memcpy(output + capacity - 4U, "...", 4U);
            return;
        }
        used += (size_t)written;
    }
    if(used == 0U) snprintf(output, capacity, "NONE");
}

static void draw_macro_preview(Canvas* canvas, const SwitchMacroApp* app) {
    if(!app->step_count || !app->steps) {
        UI_STR(canvas, 8U, 28U, "NO STEPS TO PREVIEW");
        return;
    }
    const uint16_t index = app->macro_preview_step < app->step_count ? app->macro_preview_step : 0U;
    const MacroStep* step = &app->steps[index];
    char line[64];
    char duration[12];
    char buttons[40];
    snprintf(line, sizeof(line), "STEP %u/%u", (unsigned)index + 1U, (unsigned)app->step_count);
    draw_page_header_raw(canvas, app->macro_files[app->macro_index].name, 120U, 10U, 9U, FontSecondary);
    canvas_draw_frame(canvas, 4U, 10U, 120U, 50U);
    UI_STR(canvas, 8U, 18U, line);
    format_step_buttons(step, buttons, sizeof(buttons));
    snprintf(line, sizeof(line), "BUTTON %s", buttons);
    draw_macro_filename_at(canvas, 8U, ui_translate(line), 31);
    if(macro_step_is_neutral(step)) {
        snprintf(line, sizeof(line), "WAIT / RELEASE");
    } else {
        snprintf(line, sizeof(line), "DPAD %s", macro_hat_label(step->hat));
    }
    UI_STR(canvas, 8U, 39U, line);
    snprintf(
        line,
        sizeof(line),
        "L %03u/%03u  R %03u/%03u",
        (unsigned)step->lx,
        (unsigned)step->ly,
        (unsigned)step->rx,
        (unsigned)step->ry);
    ui_draw_text(canvas, 8U, 47U, line);
    format_macro_duration(step->duration_ms, duration, sizeof(duration));
    snprintf(line, sizeof(line), "%s  %s", step->state_mode ? "HOLD" : "TAP", duration);
    UI_STR(canvas, 8U, 55U, line);
}

static void draw_macro_editor(Canvas* canvas, const SwitchMacroApp* app) {
    if(!app->step_count || !app->steps) {
        UI_STR(canvas, 8U, 28U, "NO STEPS TO EDIT");
        return;
    }
    const uint16_t index = app->macro_editor_step < app->step_count ? app->macro_editor_step : 0U;
    const MacroStep* step = &app->steps[index];
    char rows[8][28];
    snprintf(rows[0], sizeof(rows[0]), "STEP  %03u", (unsigned)index + 1U);
    snprintf(rows[1], sizeof(rows[1]), "TIME  %lu ms", (unsigned long)step->duration_ms);
    const uint8_t button_index = app->macro_editor_button % COUNT_OF(macro_edit_buttons);
    snprintf(
        rows[2],
        sizeof(rows[2]),
        "BUTTON %s  %s",
        macro_edit_buttons[button_index].name,
        (step->buttons & macro_edit_buttons[button_index].mask) ? "ON" : "OFF");
    snprintf(rows[3], sizeof(rows[3]), "MODE  %s", step->state_mode ? "HOLD" : "TAP");
    snprintf(rows[4], sizeof(rows[4]), "DPAD  %s", macro_hat_label(step->hat));
    snprintf(rows[5], sizeof(rows[5]), "DUPLICATE STEP");
    snprintf(rows[6], sizeof(rows[6]), "DELETE STEP");
    snprintf(rows[7], sizeof(rows[7]), "SAVE AND EXIT");
    char title[24];
    snprintf(title, sizeof(title), "EDIT STEP %u/%u", (unsigned)index + 1U, (unsigned)app->step_count);
    draw_page_header(canvas, title, 112U, 10U, 8U, FontSecondary);

    const uint8_t visible_rows = 5U;
    const uint8_t first = app->macro_editor_selection >= visible_rows ?
                              app->macro_editor_selection - visible_rows + 1U :
                              0U;
    for(uint8_t row = 0U; row < visible_rows; row++) {
        const uint8_t item = first + row;
        if(item >= COUNT_OF(rows)) break;
        const uint8_t y = 10U + row * 10U;
        draw_menu_item(
            canvas,
            3U,
            y,
            122U,
            9U,
            rows[item],
            NULL,
            item == app->macro_editor_selection);
    }
    draw_scrollbar(canvas, 10U, 49U, COUNT_OF(rows), first, visible_rows);
}

static void draw_macro_profile_assignment(Canvas* canvas, const SwitchMacroApp* app) {
    draw_page_header_raw(canvas, app->macro_files[app->macro_index].name, 120U, 9U, 8U, FontSecondary);
    UI_STR(canvas, 5U, 16U, "ASSIGN PROFILE");
    for(uint8_t i = 0U; i <= PROFILE_COUNT; i++) {
        const uint8_t y = 20U + i * 10U;
        const char* name = i == 0U ? "AUTO / ACTIVE" : app->settings.profile_names[i - 1U];
        const bool selected = i == app->macro_profile_selection;
        draw_menu_item(canvas, 4U, y, 120U, 9U, name, NULL, selected);
    }
}

static uint32_t info_mascot_next_random(SwitchMacroApp* app) {
    uint32_t value = app->info_mascot_random_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    app->info_mascot_random_state = value ? value : 0xA341316CU;
    return app->info_mascot_random_state;
}

static void info_mascot_choose_target(SwitchMacroApp* app) {
    uint8_t target = (uint8_t)(4U + info_mascot_next_random(app) % 105U);
    if(target == app->info_mascot_x) {
        target = target < 108U ? target + 1U : target - 1U;
    }
    app->info_mascot_target_x = target;
}

static void advance_info_mascot(SwitchMacroApp* app) {
    const uint32_t now = furi_get_tick();
    if(!app->info_mascot_initialized) {
        app->info_mascot_x = 56U;
        app->info_mascot_random_state = now ^ 0x9E3779B9U;
        if(!app->info_mascot_random_state) app->info_mascot_random_state = 0xA341316CU;
        app->info_mascot_last_step_tick = now;
        app->info_mascot_last_frame_tick = now;
        app->info_mascot_initialized = true;
        info_mascot_choose_target(app);
        app->info_mascot_facing_left = app->info_mascot_target_x < app->info_mascot_x;
    }

    const uint32_t frame_ticks = furi_ms_to_ticks(INFO_MASCOT_FRAME_MS);
    while(now - app->info_mascot_last_frame_tick >= frame_ticks) {
        app->info_mascot_last_frame_tick += frame_ticks;
        app->info_mascot_frame = (uint8_t)(
            (app->info_mascot_frame + 1U) % COUNT_OF(info_mascot_frames));
    }

    const uint32_t step_ticks = furi_ms_to_ticks(INFO_MASCOT_STEP_MS);
    while(now - app->info_mascot_last_step_tick >= step_ticks) {
        app->info_mascot_last_step_tick += step_ticks;
        if(app->info_mascot_x < app->info_mascot_target_x) {
            const uint8_t distance = app->info_mascot_target_x - app->info_mascot_x;
            const uint8_t step = distance > 4U ? 4U : distance;
            app->info_mascot_x = (uint8_t)(app->info_mascot_x + step);
            app->info_mascot_facing_left = false;
        } else if(app->info_mascot_x > app->info_mascot_target_x) {
            const uint8_t distance = app->info_mascot_x - app->info_mascot_target_x;
            const uint8_t step = distance > 4U ? 4U : distance;
            app->info_mascot_x = (uint8_t)(app->info_mascot_x - step);
            app->info_mascot_facing_left = true;
        }
        if(app->info_mascot_x == app->info_mascot_target_x) {
            info_mascot_choose_target(app);
        }
    }
}

static void draw_info_mascot(Canvas* canvas, const SwitchMacroApp* app) {
    const uint8_t* frame = info_mascot_frames[app->info_mascot_frame];
    uint8_t mirrored[32] = {0};
    // The source sprites face left. Mirror them only while walking right.
    if(!app->info_mascot_facing_left) {
        for(uint8_t y = 0U; y < 16U; y++) {
            for(uint8_t x = 0U; x < 16U; x++) {
                const uint8_t source_byte = (uint8_t)(y * 2U + x / 8U);
                if(frame[source_byte] & (uint8_t)(1U << (x % 8U))) {
                    const uint8_t mirrored_x = 15U - x;
                    const uint8_t target_byte = (uint8_t)(y * 2U + mirrored_x / 8U);
                    mirrored[target_byte] |= (uint8_t)(1U << (mirrored_x % 8U));
                }
            }
        }
        frame = mirrored;
    }
    canvas_draw_xbm(canvas, app->info_mascot_x, 48U, 16U, 16U, frame);
}

static void app_draw(Canvas* canvas, void* ctx) {
    const SwitchMacroApp* app = ctx;
    canvas_clear(canvas);
    char line[32];
    char elapsed[12];
    char total[12];
    switch(app->state) {
    case RunReady:
        draw_macro_ui(canvas, app);
        break;
    case RunSending:
        if(app->macro_ui_screen == MacroUiPlay) {
            draw_macro_play_screen(canvas, app);
        } else {
            draw_macro_playback_controller(canvas, app);
        }
        break;
    case RunLive:
        if(app->live_ui_screen == LiveUiController) {
            draw_live_controller(canvas, app);
        } else {
            draw_live_recorder(canvas, app);
        }
        break;
    case RunFinished:
        if(app->backup_loaded && app->backup_index < app->backup_count) {
            draw_page_header_raw(canvas, app->backups[app->backup_index].title, 98U, 10U, 8U, FontSecondary);
            draw_connection_status_icons(canvas, app);
            draw_live_text_fit(canvas, 8U, 28U, "Backup playback complete", 112U);
            snprintf(line, sizeof(line), "STEPS %u", (unsigned)app->step_count);
            draw_live_text_fit(canvas, 12U, 40U, line, 104U);
            format_macro_duration(app->macro_total_ms, total, sizeof(total));
            snprintf(line, sizeof(line), "TIME %s", total);
            draw_live_text_fit(canvas, 12U, 50U, line, 104U);
            draw_live_text_fit(canvas, 20U, 61U, "Back to details", 96U);
            break;
        }
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 13);
        format_macro_duration(app->macro_total_ms, total, sizeof(total));
        canvas_draw_frame(canvas, 4, 19, 120, 28);
        UI_STR(canvas, 12, 27, "STEPS");
        canvas_set_font(canvas, FontPrimary);
        snprintf(line, sizeof(line), "%u", (unsigned)app->step_count);
        ui_draw_text(canvas, 12, 38, line);
        canvas_set_font(canvas, FontSecondary);
        UI_STR(canvas, 74, 27, "DURATION");
        canvas_set_font(canvas, FontPrimary);
        ui_draw_text(canvas, 74, 38, total);
        break;
    case RunCanceled:
        if(app->backup_loaded && app->backup_index < app->backup_count) {
            draw_page_header_raw(canvas, app->backups[app->backup_index].title, 98U, 10U, 8U, FontSecondary);
            draw_connection_status_icons(canvas, app);
            draw_live_text_fit(canvas, 8U, 28U, "Backup playback stopped", 112U);
            format_macro_duration(app->macro_elapsed_ms, elapsed, sizeof(elapsed));
            snprintf(line, sizeof(line), "TIME %s", elapsed);
            draw_live_text_fit(canvas, 12U, 40U, line, 104U);
            draw_live_text_fit(canvas, 20U, 61U, "Back to details", 96U);
            break;
        }
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 13);
        format_macro_duration(app->macro_elapsed_ms, elapsed, sizeof(elapsed));
        canvas_draw_frame(canvas, 4, 19, 120, 28);
        canvas_set_font(canvas, FontSecondary);
        UI_STR(canvas, 12, 28, "STOPPED AT");
        canvas_set_font(canvas, FontPrimary);
        ui_draw_text(canvas, 12, 38, elapsed);
        break;
    case RunError:
        if(app->backup_loaded && app->backup_index < app->backup_count) {
            draw_page_header_raw(canvas, app->backups[app->backup_index].title, 98U, 10U, 8U, FontSecondary);
            draw_connection_status_icons(canvas, app);
            UI_STR(canvas, 8U, 27U, tx_failure_text(app->tx_failure));
            draw_live_text_fit(canvas, 8U, 39U, "Backup playback failed", 112U);
            draw_ui_message_banner(canvas, tx_failure_text(app->tx_failure));
            break;
        }
        UI_STR(canvas, 5, 10, tx_failure_text(app->tx_failure));
        snprintf(line, sizeof(line), "ENDPOINT %02X", app->failed_endpoint);
        UI_STR(canvas, 5, 18, line);
        draw_macro_filename(canvas, app->macro_files[app->macro_index].name, 29);
        canvas_draw_frame(canvas, 4, 35, 120, 18);
        snprintf(line, sizeof(line), "STEP %u FAILED", (unsigned)app->completed_steps + 1U);
        UI_STR(canvas, 10, 47, line);
        draw_ui_message_banner(canvas, tx_failure_text(app->tx_failure));
        break;
    }
}

static void app_input(InputEvent* event, void* ctx) {
    SwitchMacroApp* app = ctx;
    if(event->type == InputTypePress || event->type == InputTypeRelease ||
       event->type == InputTypeRepeat) {
        furi_message_queue_put(app->input_queue, event, 0);
    }
}

static bool delay_or_cancel(SwitchMacroApp* app, uint32_t delay_ms) {
    delay_ms = playback_duration_ms(delay_ms, app->macro_speed_index);
    uint32_t update_elapsed = 0U;
    while(!app->stop_requested) {
        if(app->macro_paused) {
            furi_delay_ms(10U);
            update_elapsed += 10U;
            if(update_elapsed >= 100U) {
                view_port_update(app->view_port);
                update_elapsed = 0U;
            }
            continue;
        }
        if(delay_ms == 0U) break;
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

static FuriSemaphore* endpoint_semaphore(SwitchMacroApp* app, uint8_t ep) {
    return ep == EP_GAMEPAD_IN ? app->gamepad_tx_sem : app->keyboard_tx_sem;
}

static bool send_report(SwitchMacroApp* app, uint8_t ep, const void* data, uint16_t len) {
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

static bool wait_report_complete(SwitchMacroApp* app, uint8_t ep) {
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
    SwitchMacroApp* app,
    uint16_t buttons,
    uint8_t hat,
    uint8_t lx,
    uint8_t ly,
    uint8_t rx,
    uint8_t ry) {
    SwitchReport report = neutral_gamepad();
    report.buttons = buttons;
    report.hat = hat;
    const GameProfile* profile = active_profile_const(app);
    report.lx = transform_axis(lx, profile->left_deadzone, profile->left_gain_percent);
    report.ly = transform_axis(ly, profile->left_deadzone, profile->left_gain_percent);
    report.rx = transform_axis(rx, profile->right_deadzone, profile->right_gain_percent);
    report.ry = transform_axis(ry, profile->right_deadzone, profile->right_gain_percent);
    return send_report(app, EP_GAMEPAD_IN, &report, sizeof(report));
}

static bool send_controller_wake_sequence(SwitchMacroApp* app) {
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

    furi_delay_ms(100U);
    if(!app->connected) return false;

    sent = send_gamepad_report(app, 0U, SWITCH_HAT_NONE, 0x80U, 0x80U, 0x80U, 0x80U);
    if(sent) sent = wait_report_complete(app, EP_GAMEPAD_IN);
    return sent && app->connected;
}

static void service_controller_wake(SwitchMacroApp* app) {
    if(!app->controller_wake_pending || !app->connected || !app->configured ||
       app->state != RunReady || app->live_active || app->worker) {
        return;
    }

    app->controller_wake_pending = false;
    send_controller_wake_sequence(app);
}

static void live_queue_report(SwitchMacroApp* app, const SwitchReport* report) {
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

static bool live_record_states_equivalent(const SwitchReport* left, const SwitchReport* right) {
    if(left->buttons != right->buttons || left->hat != right->hat) return false;
    const uint8_t left_axes[] = {left->lx, left->ly, left->rx, left->ry};
    const uint8_t right_axes[] = {right->lx, right->ly, right->rx, right->ry};
    for(size_t i = 0U; i < COUNT_OF(left_axes); i++) {
        const uint8_t difference = left_axes[i] > right_axes[i] ?
                                       left_axes[i] - right_axes[i] :
                                       right_axes[i] - left_axes[i];
        if(difference >= MACRO_RECORD_AXIS_EPSILON) return false;
    }
    return true;
}

static bool live_record_report_has_input(const SwitchReport* report) {
    if(report->buttons != 0U || report->hat != SWITCH_HAT_NONE) return true;
    const uint8_t axes[] = {report->lx, report->ly, report->rx, report->ry};
    for(size_t i = 0U; i < COUNT_OF(axes); i++) {
        const uint8_t difference = axes[i] > 0x80U ? axes[i] - 0x80U : 0x80U - axes[i];
        if(difference >= MACRO_RECORD_AXIS_EPSILON) return true;
    }
    return false;
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

static bool live_record_append_step_locked(SwitchMacroApp* app, const SwitchReport* report, uint32_t duration_ms) {
    if(!app->live_record_text) return false;
    if(duration_ms == 0U) duration_ms = 1U;
    while(duration_ms > 0U) {
        const uint32_t part = duration_ms > MACRO_MAX_STEP_MS ? MACRO_MAX_STEP_MS : duration_ms;
        if(app->live_record_steps >= MACRO_MAX_STEPS) {
            app->live_record_full = true;
            app->live_recording = false;
            app->live_record_paused = false;
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
                app->live_record_paused = false;
                return false;
            }
        }
        const char* hat_name = live_hat_name(report->hat);
        if(hat_name && !live_record_add_token(controls, sizeof(controls), &controls_length, hat_name)) {
            app->live_record_full = true;
            app->live_recording = false;
            app->live_record_paused = false;
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
                    app->live_record_paused = false;
                    return false;
                }
            }
        }
        if(controls_length == 0U) snprintf(controls, sizeof(controls), "NONE");

        char line[128];
        const int line_length = controls_length == 0U ?
                                    snprintf(line, sizeof(line), "WAIT %lu\n", (unsigned long)part) :
                                    snprintf(
                                        line,
                                        sizeof(line),
                                        "STATE %s %lu\n",
                                        controls,
                                        (unsigned long)part);
        if(line_length < 0 || (size_t)line_length >= sizeof(line) ||
           (size_t)line_length > MACRO_FILE_MAX_BYTES - app->live_record_bytes) {
            app->live_record_full = true;
            app->live_recording = false;
            app->live_record_paused = false;
            return false;
        }
        const uint32_t required = (uint32_t)app->live_record_bytes + (uint32_t)line_length + 1U;
        if(required > app->live_record_capacity) {
            uint32_t capacity = app->live_record_capacity ? app->live_record_capacity : LIVE_RECORD_INITIAL_CAPACITY;
            while(capacity < required && capacity < MACRO_FILE_MAX_BYTES + 1U) {
                capacity *= 2U;
                if(capacity > MACRO_FILE_MAX_BYTES + 1U) capacity = MACRO_FILE_MAX_BYTES + 1U;
            }
            char* grown = realloc(app->live_record_text, capacity);
            if(!grown) {
                app->live_record_full = true;
                app->live_recording = false;
                app->live_record_paused = false;
                return false;
            }
            app->live_record_text = grown;
            app->live_record_capacity = (uint16_t)capacity;
        }
        memcpy(app->live_record_text + app->live_record_bytes, line, (size_t)line_length);
        app->live_record_bytes += (uint16_t)line_length;
        app->live_record_text[app->live_record_bytes] = '\0';
        app->live_record_steps++;
        duration_ms -= part;
    }
    return true;
}

static void live_record_state(SwitchMacroApp* app, const SwitchReport* report, uint32_t tick) {
    if(!app->live_record_mutex || furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) != FuriStatusOk) return;
    if(app->live_latest_valid) {
        const uint16_t newly_pressed = report->buttons & (uint16_t)~app->live_latest_state.buttons;
        const uint8_t newly_pressed_hat =
            report->hat != app->live_latest_state.hat && report->hat != SWITCH_HAT_NONE ?
                report->hat : SWITCH_HAT_NONE;
        if(newly_pressed || newly_pressed_hat != SWITCH_HAT_NONE) {
            for(uint8_t i = LIVE_RECENT_INPUT_COUNT - 1U; i > 0U; i--) {
                app->live_recent_inputs[i] = app->live_recent_inputs[i - 1U];
            }
            app->live_recent_inputs[0] = (LiveRecentInput){
                .buttons = newly_pressed,
                .hat = newly_pressed_hat,
                .tick = tick,
            };
            if(app->live_recent_count < LIVE_RECENT_INPUT_COUNT) app->live_recent_count++;
        }
    }
    app->live_latest_state = *report;
    app->live_latest_valid = true;
    if(app->live_recording && !app->live_record_paused && app->live_record_text) {
        if(live_record_report_has_input(report)) app->live_record_has_input = true;
        const uint32_t elapsed_now = app->live_record_elapsed_ms + tick - app->live_record_started_tick;
        const uint32_t limit_ms = (uint32_t)recording_limit_minutes[app->settings.recording_limit_index] * 60000U;
        if(elapsed_now >= limit_ms) {
            if(app->live_record_has_state && live_record_report_has_input(&app->live_record_state)) {
                uint32_t state_duration = tick - app->live_record_last_tick;
                const uint32_t overrun = elapsed_now - limit_ms;
                state_duration = overrun < state_duration ? state_duration - overrun : 1U;
                live_record_append_step_locked(app, &app->live_record_state, state_duration);
            }
            app->live_record_elapsed_ms = limit_ms;
            app->live_recording = false;
            app->live_record_paused = false;
            app->live_record_limit_reached = true;
            app->live_record_has_state = false;
            app->live_record_started_tick = tick;
            furi_mutex_release(app->live_record_mutex);
            return;
        }
        app->live_record_elapsed_ms = elapsed_now;
        app->live_record_started_tick = tick;
        if(!app->live_record_has_state) {
            app->live_record_state = *report;
            app->live_record_last_tick = tick;
            app->live_record_has_state = true;
        } else if(!live_record_states_equivalent(&app->live_record_state, report)) {
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
    SwitchMacroApp* app = context;
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
                if(!app->live_first_frame_logged) {
                    app->live_first_frame_logged = true;
                    FURI_LOG_I(TAG, "Received first controller frame over BLE");
                }
                if(!app->live_first_input_logged &&
                   (report.buttons || report.hat != SWITCH_HAT_NONE || report.lx != 0x80U ||
                    report.ly != 0x80U || report.rx != 0x80U || report.ry != 0x80U)) {
                    app->live_first_input_logged = true;
                    FURI_LOG_I(
                        TAG,
                        "Received first active controller input over BLE: buttons=%04X hat=%u",
                        (unsigned)report.buttons,
                        (unsigned)report.hat);
                }
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
    SwitchMacroApp* app = context;
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
    if(app->view_port) view_port_update(app->view_port);
}

static int32_t live_worker(void* context) {
    SwitchMacroApp* app = context;
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
            // The serial profile spends receive credits as PC frames arrive.
            // This worker has consumed the queued report, so return the credit
            // here, outside the BLE receive callback's mutex.
            if(app->ble_profile) ble_profile_serial_notify_buffer_is_empty(app->ble_profile);
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

static bool start_live_mode(SwitchMacroApp* app) {
    if(!app->connected || !app->usb_dev || app->live_active) return false;

    if(app->live_record_mutex &&
       furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) == FuriStatusOk) {
        const bool recorder_ready = app->live_record_text == NULL;
        if(recorder_ready) {
            app->live_latest_valid = false;
            app->live_recent_count = 0U;
            memset(app->live_recent_inputs, 0, sizeof(app->live_recent_inputs));
            app->live_recording = false;
            app->live_record_paused = false;
            app->live_record_full = false;
            app->live_record_limit_reached = false;
            app->live_record_saved = false;
            app->live_record_has_state = false;
            app->live_record_saved_name[0] = '\0';
            app->live_record_elapsed_ms = 0U;
            app->ui_message[0] = '\0';
        }
        furi_mutex_release(app->live_record_mutex);
        if(!recorder_ready) return false;
    }

    app->live_ui_screen = LiveUiRecorder;
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
    app->live_first_frame_logged = false;
    app->live_first_input_logged = false;
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
    view_port_update(app->view_port);
    return true;
}

static void stop_live_mode(SwitchMacroApp* app) {
    if(!app->live_active) return;
    FURI_LOG_I(TAG, "Live shutdown: stopping input worker");
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
    // The BT service disconnects asynchronously. Wait for its GAP disconnect
    // event before replacing the serial profile; restoring it while the PC is
    // still attached can trip a Furi check during app exit.
    FURI_LOG_I(TAG, "Live shutdown: disconnect BLE central");
    bt_disconnect(app->bt);
    furi_delay_ms(200);
    FURI_LOG_I(TAG, "Live shutdown: restoring default Bluetooth profile");
    if(!bt_profile_restore_default(app->bt)) {
        FURI_LOG_E(TAG, "Failed to restore default Bluetooth profile");
    } else {
        FURI_LOG_I(TAG, "Live shutdown: default Bluetooth profile restored");
    }
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

static MacroLoadError parse_macro_text(
    char* text,
    MacroStep* steps,
    uint16_t step_capacity,
    uint16_t* step_count,
    uint16_t* error_line,
    uint32_t* total_duration_ms) {
    *step_count = 0;
    if(total_duration_ms) *total_duration_ms = 0U;
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
            if(*step_count >= step_capacity) {
                *error_line = line_number;
                return MacroLoadTooManySteps;
            }
            if(steps) steps[*step_count] = step;
            (*step_count)++;
            if(total_duration_ms) *total_duration_ms += step.duration_ms;
        }
        if(!next_line) break;
        line = next_line;
    }
    return *step_count ? MacroLoadOk : MacroLoadEmpty;
}

static MacroLoadError read_macro_text(const char* path, char** text_out, size_t* text_size_out) {
    *text_out = NULL;
    *text_size_out = 0U;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return MacroLoadStorageUnavailable;
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

    const uint64_t file_size = storage_file_size(file);
    if(file_size > MACRO_FILE_MAX_BYTES) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return MacroLoadTooLarge;
    }
    if(file_size == 0U) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return MacroLoadEmpty;
    }

    const size_t text_size = (size_t)file_size;
    char* text = malloc(text_size + 1U);
    if(!text) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return MacroLoadOutOfMemory;
    }
    size_t total = 0U;
    while(total < text_size) {
        const size_t count = storage_file_read(file, (uint8_t*)(text + total), text_size - total);
        if(count == 0U) break;
        total += count;
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(total != text_size) {
        free(text);
        return MacroLoadReadFailed;
    }
    text[total] = '\0';
    *text_out = text;
    *text_size_out = text_size;
    return MacroLoadOk;
}

static MacroLoadError load_macro_steps(
    const char* path,
    MacroStep** steps_out,
    uint16_t* step_count_out,
    uint16_t* error_line_out) {
    *steps_out = NULL;
    *step_count_out = 0U;
    *error_line_out = 0U;
    char* text = NULL;
    size_t text_size = 0U;
    const MacroLoadError read_result = read_macro_text(path, &text, &text_size);
    if(read_result != MacroLoadOk) return read_result;

    uint16_t step_count = 0U;
    uint16_t error_line = 0U;
    const MacroLoadError result = parse_macro_text(
        text,
        NULL,
        MACRO_MAX_STEPS,
        &step_count,
        &error_line,
        NULL);
    free(text);
    if(result != MacroLoadOk) return result;

    // Count/validate before allocating the exact step array. Then release the
    // file buffer before allocating steps to keep peak heap use low.
    MacroStep* steps = malloc(sizeof(*steps) * step_count);
    if(!steps) return MacroLoadOutOfMemory;
    text = NULL;
    text_size = 0U;
    const MacroLoadError second_read = read_macro_text(path, &text, &text_size);
    UNUSED(text_size);
    if(second_read != MacroLoadOk) {
        free(steps);
        return second_read;
    }
    uint16_t parsed_count = 0U;
    const MacroLoadError second_result = parse_macro_text(
        text,
        steps,
        step_count,
        &parsed_count,
        &error_line,
        NULL);
    free(text);
    if(second_result != MacroLoadOk || parsed_count != step_count) {
        free(steps);
        *error_line_out = error_line;
        return second_result == MacroLoadOk ? MacroLoadReadFailed : second_result;
    }
    *steps_out = steps;
    *step_count_out = step_count;
    return MacroLoadOk;
}

static MacroLoadError load_macro_file(SwitchMacroApp* app, const char* path) {
    MacroStep* steps = NULL;
    uint16_t step_count = 0U;
    uint16_t error_line = 0U;
    const MacroLoadError error = load_macro_steps(path, &steps, &step_count, &error_line);
    app->load_error_line = error_line;
    if(error != MacroLoadOk) return error;
    app->steps = steps;
    app->step_count = step_count;
    return MacroLoadOk;
}

static const char* macro_load_error_text(MacroLoadError error) {
    switch(error) {
    case MacroLoadMissing: return "Macro file not found";
    case MacroLoadTooLarge: return "Macro file over 48 KB";
    case MacroLoadEmpty: return "Macro file has no steps";
    case MacroLoadBadSyntax: return "Invalid macro syntax";
    case MacroLoadTooManySteps: return "More than 1024 steps";
    case MacroLoadOutOfMemory: return "Not enough memory";
    case MacroLoadStorageUnavailable: return "Storage unavailable";
    case MacroLoadReadFailed: return "Could not read macro file";
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

static bool macro_name_has_suffix(const char* name, const char* suffix);

static void backup_path(char* path, size_t capacity, const char* name) {
    snprintf(path, capacity, "%s/%s", BACKUP_DIRECTORY, name);
}

static void backup_metadata_filename(const char* backup_filename, char* output, size_t capacity) {
    snprintf(output, capacity, "%s", backup_filename);
    const size_t length = strlen(output);
    if(length >= 4U && strcmp(output + length - 4U, ".bak") == 0) {
        memcpy(output + length - 4U, ".meta", 6U);
    }
}

static void backup_current_timestamp(uint32_t* timestamp, char* date_time, size_t capacity) {
    DateTime now = {0};
    furi_hal_rtc_get_datetime(&now);
    if(now.year < 2020U || now.month == 0U || now.day == 0U) {
        *timestamp = 0U;
        snprintf(date_time, capacity, "Date/time not set");
        return;
    }
    *timestamp = datetime_datetime_to_timestamp(&now);
    const unsigned month = now.month <= 12U ? now.month : 12U;
    const unsigned day = now.day <= 31U ? now.day : 31U;
    const unsigned hour = now.hour <= 23U ? now.hour : 23U;
    const unsigned minute = now.minute <= 59U ? now.minute : 59U;
    snprintf(
        date_time,
        capacity,
        "%04u-%02u-%02u %02u:%02u",
        (unsigned)now.year,
        month,
        day,
        hour,
        minute);
}

static bool write_backup_metadata(Storage* storage, const MacroBackup* backup) {
    char filename[MACRO_FILENAME_MAX];
    char path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    char contents[128];
    backup_metadata_filename(backup->filename, filename, sizeof(filename));
    backup_path(path, sizeof(path), filename);
    const int length = snprintf(
        contents,
        sizeof(contents),
        "%s\n%s\n%lu\n%u\n%lu\n",
        backup->title,
        backup->date_time,
        (unsigned long)backup->timestamp,
        (unsigned)backup->step_count,
        (unsigned long)backup->duration_ms);
    if(length <= 0 || (size_t)length >= sizeof(contents)) return false;
    File* file = storage_file_alloc(storage);
    if(!file) return false;
    const bool opened = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    const bool written = opened &&
                         storage_file_write(file, contents, (size_t)length) == (size_t)length &&
                         storage_file_sync(file);
    const bool closed = storage_file_close(file);
    storage_file_free(file);
    return written && closed;
}

static bool read_backup_metadata(Storage* storage, const char* filename, MacroBackup* backup) {
    char metadata_filename[MACRO_FILENAME_MAX];
    char path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    char contents[128] = {0};
    unsigned long timestamp = 0UL;
    unsigned long duration_ms = 0UL;
    unsigned steps = 0U;
    backup_metadata_filename(filename, metadata_filename, sizeof(metadata_filename));
    backup_path(path, sizeof(path), metadata_filename);
    File* file = storage_file_alloc(storage);
    if(!file) return false;
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_close(file);
        storage_file_free(file);
        return false;
    }
    const size_t bytes_read = storage_file_read(file, contents, sizeof(contents) - 1U);
    storage_file_close(file);
    storage_file_free(file);
    if(bytes_read == 0U) return false;
    contents[bytes_read] = '\0';
    const int parsed = sscanf(
        contents,
        "%27[^\n]\n%19[^\n]\n%lu\n%u\n%lu",
        backup->title,
        backup->date_time,
        &timestamp,
        &steps,
        &duration_ms);
    if(parsed != 5) return false;
    snprintf(backup->filename, sizeof(backup->filename), "%s", filename);
    backup->timestamp = (uint32_t)timestamp;
    backup->step_count = steps > UINT16_MAX ? UINT16_MAX : (uint16_t)steps;
    backup->duration_ms = (uint32_t)duration_ms;
    return backup->title[0] != '\0';
}

static bool remove_backup_files(Storage* storage, const MacroBackup* backup) {
    char data_path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    char metadata_filename[MACRO_FILENAME_MAX];
    char metadata_path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    backup_path(data_path, sizeof(data_path), backup->filename);
    backup_metadata_filename(backup->filename, metadata_filename, sizeof(metadata_filename));
    backup_path(metadata_path, sizeof(metadata_path), metadata_filename);
    const bool data_removed = !storage_common_exists(storage, data_path) ||
                              storage_common_remove(storage, data_path) == FSE_OK;
    const bool metadata_removed = !storage_common_exists(storage, metadata_path) ||
                                  storage_common_remove(storage, metadata_path) == FSE_OK;
    return data_removed && metadata_removed;
}

static void refresh_backups(SwitchMacroApp* app) {
    app->backup_count = 0U;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return;
    if(!storage_dir_exists(storage, APP_DATA_DIRECTORY)) storage_common_mkdir(storage, APP_DATA_DIRECTORY);
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) storage_common_mkdir(storage, MACRO_DIRECTORY);
    if(!storage_dir_exists(storage, BACKUP_DIRECTORY)) storage_common_mkdir(storage, BACKUP_DIRECTORY);
    File* directory = storage_file_alloc(storage);
    if(!directory || !storage_dir_open(directory, BACKUP_DIRECTORY)) {
        if(directory) {
            storage_dir_close(directory);
            storage_file_free(directory);
        }
        furi_record_close(RECORD_STORAGE);
        return;
    }
    FileInfo info;
    char filename[MACRO_FILENAME_MAX];
    while(storage_dir_read(directory, &info, filename, sizeof(filename))) {
        filename[sizeof(filename) - 1U] = '\0';
        if((info.flags & FSF_DIRECTORY) || !macro_name_has_suffix(filename, ".bak")) continue;
        MacroBackup backup = {0};
        if(!read_backup_metadata(storage, filename, &backup)) continue;
        if(app->backup_count < BACKUP_MAX_FILES) app->backups[app->backup_count++] = backup;
    }
    storage_dir_close(directory);
    storage_file_free(directory);
    furi_record_close(RECORD_STORAGE);

    for(uint8_t i = 1U; i < app->backup_count; i++) {
        MacroBackup current = app->backups[i];
        uint8_t j = i;
        while(j > 0U && app->backups[j - 1U].timestamp < current.timestamp) {
            app->backups[j] = app->backups[j - 1U];
            j--;
        }
        app->backups[j] = current;
    }
    if(app->backup_index >= app->backup_count) app->backup_index = app->backup_count ? app->backup_count - 1U : 0U;
    const uint8_t visible = BACKUP_LIST_VISIBLE;
    app->backup_offset = app->backup_index >= visible ? app->backup_index - visible + 1U : 0U;
}

static bool next_backup_filename(Storage* storage, char* filename, size_t capacity) {
    char path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    for(uint16_t index = 1U; index <= 9999U; index++) {
        snprintf(filename, capacity, "backup_%04u.bak", (unsigned)index);
        backup_path(path, sizeof(path), filename);
        if(!storage_common_exists(storage, path)) return true;
    }
    return false;
}

static bool move_macro_to_backup_with_stats(
    SwitchMacroApp* app,
    const char* macro_name,
    uint16_t step_count,
    uint32_t duration_ms,
    const char* protected_backup_filename) {
    if(!macro_name || !macro_name[0]) return false;
    refresh_backups(app);
    MacroBackup oldest = {0};
    bool evict_oldest = app->backup_count >= BACKUP_MAX_FILES;
    if(evict_oldest) {
        int8_t oldest_index = app->backup_count - 1U;
        if(protected_backup_filename &&
           strcmp(app->backups[oldest_index].filename, protected_backup_filename) == 0) {
            oldest_index--;
        }
        if(oldest_index < 0) evict_oldest = false;
        else oldest = app->backups[oldest_index];
    }
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        snprintf(app->ui_message, sizeof(app->ui_message), "SD unavailable");
        return false;
    }
    if(!storage_dir_exists(storage, BACKUP_DIRECTORY)) storage_common_mkdir(storage, BACKUP_DIRECTORY);
    MacroBackup backup = {0};
    if(!next_backup_filename(storage, backup.filename, sizeof(backup.filename))) {
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Backup is full");
        return false;
    }
    const size_t name_length = strlen(macro_name);
    const size_t base_length = name_length >= 4U && strcmp(macro_name + name_length - 4U, ".txt") == 0 ?
                                   name_length - 4U : name_length;
    if(base_length == 0U || base_length > MACRO_BASENAME_MAX) {
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Invalid macro name");
        return false;
    }
    memcpy(backup.title, macro_name, base_length);
    backup.title[base_length] = '\0';
    backup.step_count = step_count;
    backup.duration_ms = duration_ms;
    backup_current_timestamp(&backup.timestamp, backup.date_time, sizeof(backup.date_time));
    char source[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    char destination[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    macro_path(source, sizeof(source), macro_name);
    backup_path(destination, sizeof(destination), backup.filename);
    bool saved = storage_common_rename(storage, source, destination) == FSE_OK;
    if(saved) saved = write_backup_metadata(storage, &backup);
    if(saved && evict_oldest) (void)remove_backup_files(storage, &oldest);
    if(!saved) {
        if(storage_common_exists(storage, destination)) storage_common_rename(storage, destination, source);
        storage_common_remove(storage, destination);
    }
    furi_record_close(RECORD_STORAGE);
    if(!saved) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Backup move failed");
        write_error_log(app, "Macro backup move failed");
        return false;
    }
    refresh_backups(app);
    app->ui_message[0] = '\0';
    return true;
}

static bool move_macro_to_backup(SwitchMacroApp* app, const char* macro_name) {
    return move_macro_to_backup_with_stats(
        app,
        macro_name,
        app->step_count,
        app->macro_total_ms,
        NULL);
}

static bool read_macro_summary(
    const char* path,
    uint16_t* step_count,
    uint32_t* duration_ms) {
    char* text = NULL;
    size_t text_size = 0U;
    if(read_macro_text(path, &text, &text_size) != MacroLoadOk) return false;
    UNUSED(text_size);
    uint16_t parsed_count = 0U;
    uint16_t error_line = 0U;
    uint32_t duration = 0U;
    const MacroLoadError result = parse_macro_text(
        text,
        NULL,
        MACRO_MAX_STEPS,
        &parsed_count,
        &error_line,
        &duration);
    free(text);
    if(result != MacroLoadOk) return false;
    *step_count = parsed_count;
    *duration_ms = duration;
    return true;
}

static int8_t find_backup_by_filename(const SwitchMacroApp* app, const char* filename) {
    for(uint8_t i = 0U; i < app->backup_count; i++) {
        if(strcmp(app->backups[i].filename, filename) == 0) return (int8_t)i;
    }
    return -1;
}

static bool load_selected_backup(SwitchMacroApp* app) {
    if(app->backup_index >= app->backup_count) return false;
    if(app->steps) {
        free(app->steps);
        app->steps = NULL;
    }
    char path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    backup_path(path, sizeof(path), app->backups[app->backup_index].filename);
    app->load_error = load_macro_file(app, path);
    if(app->load_error != MacroLoadOk) {
        snprintf(app->ui_message, sizeof(app->ui_message), "%s", macro_load_error_text(app->load_error));
        app->backup_loaded = false;
        return false;
    }
    app->macro_total_ms = 0U;
    for(uint16_t i = 0U; i < app->step_count; i++) {
        app->macro_total_ms += app->steps[i].duration_ms;
    }
    app->backup_loaded = true;
    app->backup_preview_step = 0U;
    app->backup_preview_step_started = furi_get_tick();
    app->backup_preview_playing = true;
    app->ui_message[0] = '\0';
    return true;
}

static bool restore_selected_backup(SwitchMacroApp* app, const char* basename, bool overwrite) {
    if(app->backup_index >= app->backup_count || !basename || !basename[0]) return false;
    const size_t base_length = strlen(basename);
    if(base_length > MACRO_BASENAME_MAX || strchr(basename, '/') || strchr(basename, '\\')) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Invalid macro name");
        return false;
    }
    char target_name[MACRO_FILENAME_MAX];
    snprintf(target_name, sizeof(target_name), "%s.txt", basename);
    char target_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    macro_path(target_path, sizeof(target_path), target_name);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        snprintf(app->ui_message, sizeof(app->ui_message), "SD unavailable");
        return false;
    }
    const bool target_exists = storage_common_exists(storage, target_path);
    furi_record_close(RECORD_STORAGE);
    if(target_exists && !overwrite) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Name already exists");
        return false;
    }

    char selected_backup_filename[MACRO_FILENAME_MAX];
    snprintf(
        selected_backup_filename,
        sizeof(selected_backup_filename),
        "%s",
        app->backups[app->backup_index].filename);

    if(target_exists && overwrite) {
        uint16_t old_steps = 0U;
        uint32_t old_duration = 0U;
        if(!read_macro_summary(target_path, &old_steps, &old_duration)) {
            snprintf(app->ui_message, sizeof(app->ui_message), "Cannot compare macros");
            return false;
        }
        if(!move_macro_to_backup_with_stats(
               app,
               target_name,
               old_steps,
               old_duration,
               selected_backup_filename)) {
            return false;
        }
        const int8_t refreshed_index = find_backup_by_filename(app, selected_backup_filename);
        if(refreshed_index < 0) {
            snprintf(app->ui_message, sizeof(app->ui_message), "Backup no longer available");
            return false;
        }
        app->backup_index = (uint8_t)refreshed_index;
    }

    MacroBackup selected_backup = app->backups[app->backup_index];
    char source_path[sizeof(BACKUP_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    backup_path(source_path, sizeof(source_path), selected_backup.filename);
    storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        snprintf(app->ui_message, sizeof(app->ui_message), "SD unavailable");
        return false;
    }
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) storage_common_mkdir(storage, MACRO_DIRECTORY);
    const bool restored = storage_common_rename(storage, source_path, target_path) == FSE_OK;
    if(restored) (void)remove_backup_files(storage, &selected_backup);
    furi_record_close(RECORD_STORAGE);
    if(!restored) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Restore failed");
        return false;
    }

    if(app->steps) {
        free(app->steps);
        app->steps = NULL;
    }
    app->step_count = 0U;
    app->backup_loaded = false;
    refresh_macro_files(app);
    refresh_backups(app);
    app->macro_ui_screen = MacroUiBackupList;
    app->ui_message[0] = '\0';
    return true;
}

static bool restore_backup_with_suffix(SwitchMacroApp* app) {
    if(app->backup_index >= app->backup_count) return false;
    const char* base = app->backups[app->backup_index].title;
    char candidate[MACRO_BASENAME_MAX + 1U];
    for(uint16_t suffix = 2U; suffix <= 999U; suffix++) {
        char suffix_text[8];
        snprintf(suffix_text, sizeof(suffix_text), "_%u", (unsigned)suffix);
        const size_t suffix_length = strlen(suffix_text);
        const size_t prefix_length = strlen(base) < MACRO_BASENAME_MAX - suffix_length ?
                                         strlen(base) : MACRO_BASENAME_MAX - suffix_length;
        memcpy(candidate, base, prefix_length);
        memcpy(candidate + prefix_length, suffix_text, suffix_length + 1U);
        if(restore_selected_backup(app, candidate, false)) return true;
    }
    snprintf(app->ui_message, sizeof(app->ui_message), "No free backup name");
    return false;
}

static void begin_backup_restore(SwitchMacroApp* app) {
    if(!load_selected_backup(app)) return;
    const MacroBackup* backup = &app->backups[app->backup_index];
    snprintf(app->backup_restore_name, sizeof(app->backup_restore_name), "%s", backup->title);
    char target_name[MACRO_FILENAME_MAX];
    char target_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    snprintf(target_name, sizeof(target_name), "%s.txt", backup->title);
    macro_path(target_path, sizeof(target_path), target_name);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool exists = storage && storage_common_exists(storage, target_path);
    if(storage) furi_record_close(RECORD_STORAGE);
    if(!exists) {
        (void)restore_selected_backup(app, backup->title, false);
        return;
    }
    if(!read_macro_summary(target_path, &app->backup_existing_steps, &app->backup_existing_total_ms)) {
        app->backup_existing_steps = 0U;
        app->backup_existing_total_ms = 0U;
    }
    app->backup_conflict_selection = 0U;
    app->macro_ui_screen = MacroUiBackupConflict;
    app->ui_message[0] = '\0';
}

static void begin_backup_rename(SwitchMacroApp* app) {
    if(app->backup_index >= app->backup_count) return;
    snprintf(app->rename_name, sizeof(app->rename_name), "%s", app->backups[app->backup_index].title);
    app->rename_position = (uint8_t)strlen(app->rename_name);
    app->rename_keyboard_row = 0U;
    app->rename_keyboard_column = 0U;
    app->rename_symbols = false;
    app->rename_lowercase = false;
    app->rename_cursor_selected = false;
    app->rename_target = RenameTargetRestore;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiRename;
}

static void delete_selected_backup(SwitchMacroApp* app) {
    if(app->backup_index >= app->backup_count) return;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage || !remove_backup_files(storage, &app->backups[app->backup_index])) {
        if(storage) furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Backup delete failed");
        return;
    }
    furi_record_close(RECORD_STORAGE);
    if(app->steps) {
        free(app->steps);
        app->steps = NULL;
    }
    app->backup_loaded = false;
    refresh_backups(app);
    app->macro_ui_screen = MacroUiBackupList;
    snprintf(app->ui_message, sizeof(app->ui_message), "Backup deleted");
}

static bool macro_name_has_suffix(const char* name, const char* suffix) {
    const size_t name_length = strlen(name);
    const size_t suffix_length = strlen(suffix);
    if(name_length < suffix_length) return false;
    name += name_length - suffix_length;
    for(size_t i = 0U; i < suffix_length; i++) {
        if(tolower((unsigned char)name[i]) != tolower((unsigned char)suffix[i])) return false;
    }
    return true;
}

static bool recover_macro_edit_files(Storage* storage) {
    File* directory = storage_file_alloc(storage);
    if(!directory) return false;
    if(!storage_dir_open(directory, MACRO_DIRECTORY)) {
        storage_file_free(directory);
        return false;
    }

    // Keep this scratch buffer out of the 6 KB app thread stack. Recovery is
    // only called by the main app thread and never recursively.
    static char backup_names[MACRO_MAX_FILES][MACRO_FILENAME_MAX + 8U];
    uint8_t backup_count = 0U;
    FileInfo info;
    char name[MACRO_FILENAME_MAX + 8U];
    while(storage_dir_read(directory, &info, name, sizeof(name))) {
        name[sizeof(name) - 1U] = '\0';
        if((info.flags & FSF_DIRECTORY) || !macro_name_has_suffix(name, ".txt.bak")) continue;
        if(backup_count < MACRO_MAX_FILES) {
            snprintf(backup_names[backup_count], sizeof(backup_names[backup_count]), "%s", name);
            backup_count++;
        }
    }
    storage_dir_close(directory);
    storage_file_free(directory);

    bool recovered = true;
    for(uint8_t i = 0U; i < backup_count; i++) {
        char original_name[MACRO_FILENAME_MAX];
        const size_t backup_length = strlen(backup_names[i]);
        const size_t original_length = backup_length - 4U;
        if(original_length >= sizeof(original_name)) {
            recovered = false;
            continue;
        }
        memcpy(original_name, backup_names[i], original_length);
        original_name[original_length] = '\0';
        if(!macro_name_has_txt_extension(original_name)) continue;

        char original_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
        char backup_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 8U];
        char temp_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 8U];
        macro_path(original_path, sizeof(original_path), original_name);
        macro_path(backup_path, sizeof(backup_path), backup_names[i]);
        snprintf(temp_path, sizeof(temp_path), "%s.tmp", original_path);
        if(storage_common_exists(storage, original_path)) {
            if(storage_common_remove(storage, backup_path) != FSE_OK) recovered = false;
        } else if(storage_common_rename(storage, backup_path, original_path) != FSE_OK) {
            recovered = false;
            continue;
        }
        if(storage_common_exists(storage, temp_path) &&
           storage_common_remove(storage, temp_path) != FSE_OK) {
            recovered = false;
        }
    }
    return recovered;
}

static void migrate_game_profiles(GameProfile* destination, const GameProfileV3* source) {
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        destination[i].left_deadzone = source[i].deadzone;
        destination[i].right_deadzone = source[i].deadzone;
        destination[i].left_gain_percent = source[i].gain_percent;
        destination[i].right_gain_percent = source[i].gain_percent;
        destination[i].simulator_step = source[i].simulator_step;
        destination[i].playback_speed_index = source[i].playback_speed_index;
    }
}

static void load_app_settings(SwitchMacroApp* app) {
    settings_set_defaults(&app->settings);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return;
    File* file = storage_file_alloc(storage);
    if(!file) {
        furi_record_close(RECORD_STORAGE);
        return;
    }

    uint8_t* loaded = malloc(sizeof(app->settings));
    if(!loaded) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }
    size_t bytes_read = 0U;
    if(storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        bytes_read = storage_file_read(file, loaded, sizeof(app->settings));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    if(bytes_read == sizeof(app->settings)) {
        AppSettings* settings = (AppSettings*)loaded;
        // Older builds stored extra locale IDs. Keep their other preferences,
        // but normalize unsupported languages to English.
        if(settings->language >= UiLanguageCount) settings->language = UiLanguageEnglish;
        if(settings->recording_limit_index >= RECORDING_LIMIT_COUNT) {
            settings->recording_limit_index = 0U;
        }
        if(settings_valid(settings)) app->settings = *settings;
    } else if(bytes_read == sizeof(AppSettingsV3)) {
        const AppSettingsV3* previous = (const AppSettingsV3*)loaded;
        if(settings_v3_valid(previous)) {
            settings_set_defaults(&app->settings);
            app->settings.active_profile = previous->active_profile;
            migrate_game_profiles(app->settings.profiles, previous->profiles);
            memcpy(app->settings.profile_names, previous->profile_names, sizeof(previous->profile_names));
            app->settings.recent_clock = previous->recent_clock;
            memcpy(app->settings.macros, previous->macros, sizeof(previous->macros));
            app->settings.language = previous->language;
            app->settings.controller_layout = previous->controller_layout;
            app->settings.sound_enabled = previous->sound_enabled;
            app->settings.keep_screen_on = previous->keep_screen_on;
        }
    } else if(bytes_read == sizeof(AppSettingsV2)) {
        const AppSettingsV2* previous = (const AppSettingsV2*)loaded;
        if(settings_v2_valid(previous)) {
            settings_set_defaults(&app->settings);
            app->settings.active_profile = previous->active_profile;
            migrate_game_profiles(app->settings.profiles, previous->profiles);
            memcpy(app->settings.profile_names, previous->profile_names, sizeof(previous->profile_names));
            app->settings.recent_clock = previous->recent_clock;
            memcpy(app->settings.macros, previous->macros, sizeof(previous->macros));
        }
    } else if(bytes_read == sizeof(LegacyAppSettings)) {
        LegacyAppSettings legacy = {0};
        memcpy(&legacy, loaded, sizeof(legacy));
        if(legacy_settings_valid(&legacy)) {
            app->settings.active_profile = legacy.active_profile;
            migrate_game_profiles(app->settings.profiles, legacy.profiles);
        }
    }
    if(app->settings.language >= UiLanguageCount) app->settings.language = UiLanguageEnglish;
    free(loaded);
    ui_language = (UiLanguage)app->settings.language;
}

static bool save_app_settings(SwitchMacroApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return false;
    if(!storage_dir_exists(storage, APP_DATA_DIRECTORY)) storage_common_mkdir(storage, APP_DATA_DIRECTORY);
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) storage_common_mkdir(storage, MACRO_DIRECTORY);

    File* file = storage_file_alloc(storage);
    if(!file) {
        furi_record_close(RECORD_STORAGE);
        return false;
    }
    const bool opened = storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    const size_t bytes_written = opened ? storage_file_write(file, &app->settings, sizeof(app->settings)) : 0U;
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return bytes_written == sizeof(app->settings);
}

static void write_error_log(SwitchMacroApp* app, const char* event) {
    if(!app->error_logging_enabled || !event || !event[0]) return;

    const uint32_t now = furi_get_tick();
    if(app->error_log_has_previous) {
        const uint32_t elapsed = now - app->error_log_last_tick;
        if((strcmp(app->error_log_last_event, event) == 0 && elapsed < furi_ms_to_ticks(60000U)) ||
           elapsed < furi_ms_to_ticks(2000U)) {
            return;
        }
    }

    char line[72];
    const int line_length = snprintf(line, sizeof(line), "%lu %s\n", (unsigned long)now, event);
    if(line_length <= 0 || (size_t)line_length >= sizeof(line)) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return;
    if(!storage_dir_exists(storage, APP_DATA_DIRECTORY)) storage_common_mkdir(storage, APP_DATA_DIRECTORY);
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) storage_common_mkdir(storage, MACRO_DIRECTORY);

    File* file = storage_file_alloc(storage);
    if(!file) {
        furi_record_close(RECORD_STORAGE);
        return;
    }
    bool opened = storage_file_open(file, ERROR_LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND);
    if(opened && storage_file_size(file) + (size_t)line_length > ERROR_LOG_MAX_BYTES) {
        storage_file_close(file);
        storage_file_free(file);
        file = storage_file_alloc(storage);
        opened = file && storage_file_open(file, ERROR_LOG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    }
    const bool written = opened &&
                         storage_file_write(file, line, (size_t)line_length) == (size_t)line_length &&
                         storage_file_sync(file);
    if(file) {
        storage_file_close(file);
        storage_file_free(file);
    }
    furi_record_close(RECORD_STORAGE);

    if(written) {
        app->error_log_has_previous = true;
        app->error_log_last_tick = now;
        snprintf(app->error_log_last_event, sizeof(app->error_log_last_event), "%s", event);
    }
}

static void refresh_macro_files(SwitchMacroApp* app) {
    char previous_name[MACRO_FILENAME_MAX] = {0};
    if(app->macro_count > 0U && app->macro_index < app->macro_count) {
        snprintf(previous_name, sizeof(previous_name), "%s", app->macro_files[app->macro_index].name);
    }
    uint8_t all_count = 0U;
    memset(app->macro_files, 0, sizeof(app->macro_files));
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
    const bool recovery_ok = recover_macro_edit_files(storage);

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
        if(all_count < MACRO_MAX_FILES) {
            snprintf(app->macro_files[all_count].name, MACRO_FILENAME_MAX, "%s", name);
            all_count++;
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

    for(uint8_t i = 1U; i < all_count; i++) {
        MacroFile current = app->macro_files[i];
        uint8_t j = i;
        while(j > 0U && macro_name_compare(app->macro_files[j - 1U].name, current.name) > 0) {
            app->macro_files[j] = app->macro_files[j - 1U];
            j--;
        }
        app->macro_files[j] = current;
    }

    for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
        MacroPreference* preference = &app->settings.macros[i];
        if(!preference->name[0]) continue;
        bool found = false;
        for(uint8_t j = 0U; j < all_count; j++) {
            if(strcmp(preference->name, app->macro_files[j].name) == 0) {
                found = true;
                break;
            }
        }
        if(!found) macro_preference_remove(app, preference->name);
    }

    app->macro_total_count = all_count;
    uint8_t filtered_count = 0U;
    for(uint8_t i = 0U; i < all_count; i++) {
        const MacroPreference* preference =
            macro_preference_find_const(app, app->macro_files[i].name);
        const bool include = app->macro_list_mode == MacroListAll ||
                             (app->macro_list_mode == MacroListFavorites && preference &&
                              preference->favorite) ||
                             (app->macro_list_mode == MacroListRecent && preference &&
                              preference->recent_stamp != 0U);
        if(include) {
            if(filtered_count != i) app->macro_files[filtered_count] = app->macro_files[i];
            filtered_count++;
        }
    }
    app->macro_count = filtered_count;
    if(app->macro_list_mode == MacroListRecent) {
        for(uint8_t i = 1U; i < app->macro_count; i++) {
            MacroFile current = app->macro_files[i];
            const MacroPreference* current_preference = macro_preference_find_const(app, current.name);
            uint8_t j = i;
            while(j > 0U) {
                const MacroPreference* previous_preference =
                    macro_preference_find_const(app, app->macro_files[j - 1U].name);
                if(!current_preference || !previous_preference ||
                   previous_preference->recent_stamp >= current_preference->recent_stamp) {
                    break;
                }
                app->macro_files[j] = app->macro_files[j - 1U];
                j--;
            }
            app->macro_files[j] = current;
        }
    }

    app->macro_index = 0U;
    for(uint8_t i = 0U; i < app->macro_count; i++) {
        if(previous_name[0] && strcmp(app->macro_files[i].name, previous_name) == 0) {
            app->macro_index = i;
            break;
        }
        if(strcmp(app->macro_files[i].name, MACRO_DEFAULT_NAME) == 0) {
            app->macro_index = i;
        }
    }
    normalize_macro_list_offset(app);
    app->ui_message[0] = '\0';
    if(!recovery_ok) snprintf(app->ui_message, sizeof(app->ui_message), "Macro recovery failed");
}

static bool start_live_recording(SwitchMacroApp* app) {
    if(!app->live_record_mutex) return false;
    if(app->live_record_pending_text) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Save recording first");
        return false;
    }
    char* text = malloc(LIVE_RECORD_INITIAL_CAPACITY);
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
    app->live_record_capacity = LIVE_RECORD_INITIAL_CAPACITY;
    app->live_record_bytes = (uint16_t)header_length;
    app->live_record_steps = 0U;
    app->live_record_full = false;
    app->live_record_limit_reached = false;
    app->live_record_saved = false;
    app->live_record_paused = false;
    app->live_record_has_input = false;
    app->live_record_saved_name[0] = '\0';
    app->live_record_started_tick = furi_get_tick();
    app->live_record_elapsed_ms = 0U;
    app->live_recording = true;
    app->ui_message[0] = '\0';
    app->live_record_has_state = app->live_latest_valid;
    if(app->live_latest_valid) {
        app->live_record_state = app->live_latest_state;
        app->live_record_has_input = live_record_report_has_input(&app->live_latest_state);
        app->live_record_last_tick = furi_get_tick();
    }
    furi_mutex_release(app->live_record_mutex);
    return true;
}

static bool toggle_live_record_pause(SwitchMacroApp* app) {
    if(!app->live_record_mutex || furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) != FuriStatusOk) {
        return false;
    }
    if(!app->live_recording || !app->live_record_text) {
        furi_mutex_release(app->live_record_mutex);
        return false;
    }

    const uint32_t now = furi_get_tick();
    if(!app->live_record_paused) {
        app->live_record_elapsed_ms += now - app->live_record_started_tick;
        if(app->live_record_has_state && live_record_report_has_input(&app->live_record_state) &&
           !live_record_append_step_locked(
               app,
               &app->live_record_state,
               now - app->live_record_last_tick)) {
            app->live_record_paused = false;
            app->live_record_has_state = false;
            furi_mutex_release(app->live_record_mutex);
            return false;
        }
        app->live_record_paused = true;
        app->live_record_has_state = false;
    } else {
        app->live_record_started_tick = now;
        app->live_record_paused = false;
        app->live_record_has_state = app->live_latest_valid;
        if(app->live_latest_valid) {
            app->live_record_state = app->live_latest_state;
            app->live_record_last_tick = now;
        }
    }
    furi_mutex_release(app->live_record_mutex);
    return true;
}

static bool finish_live_recording(SwitchMacroApp* app) {
    if(!app->live_record_mutex || furi_mutex_acquire(app->live_record_mutex, FuriWaitForever) != FuriStatusOk) {
        return false;
    }
    char* text = app->live_record_text;
    if(!text) {
        furi_mutex_release(app->live_record_mutex);
        return false;
    }
    if(app->live_recording && !app->live_record_paused) {
        app->live_record_elapsed_ms += furi_get_tick() - app->live_record_started_tick;
    }
    if(app->live_recording && !app->live_record_paused && app->live_record_has_state &&
       live_record_report_has_input(&app->live_record_state)) {
        live_record_append_step_locked(
            app,
            &app->live_record_state,
            furi_get_tick() - app->live_record_last_tick);
    }
    app->live_recording = false;
    app->live_record_paused = false;
    app->live_record_full = false;
    app->live_record_limit_reached = false;
    app->live_record_has_state = false;
    const bool has_input = app->live_record_has_input;
    app->live_record_has_input = false;
    app->live_record_text = NULL;
    app->live_record_capacity = 0U;
    const uint16_t byte_count = app->live_record_bytes;
    const uint16_t step_count = app->live_record_steps;
    app->live_record_bytes = 0U;
    furi_mutex_release(app->live_record_mutex);

    if(step_count == 0U || !has_input) {
        free(text);
        snprintf(app->ui_message, sizeof(app->ui_message), "No input to save");
        return false;
    }

    app->live_record_pending_text = text;
    app->live_record_pending_bytes = byte_count;
    app->live_record_pending_steps = step_count;
    app->ui_message[0] = '\0';
    return true;
}

static bool save_pending_live_recording(SwitchMacroApp* app, const char* basename) {
    if(!app->live_record_pending_text || !app->live_record_pending_bytes) return false;
    char saved_name[MACRO_FILENAME_MAX] = {0};
    char path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        snprintf(app->ui_message, sizeof(app->ui_message), "SD unavailable");
        return false;
    }
    if(!storage_dir_exists(storage, APP_DATA_DIRECTORY)) storage_common_mkdir(storage, APP_DATA_DIRECTORY);
    if(!storage_dir_exists(storage, MACRO_DIRECTORY)) storage_common_mkdir(storage, MACRO_DIRECTORY);

    if(basename) {
        const size_t length = strlen(basename);
        if(length == 0U || length > MACRO_BASENAME_MAX || strchr(basename, '/') || strchr(basename, '\\')) {
            furi_record_close(RECORD_STORAGE);
            snprintf(app->ui_message, sizeof(app->ui_message), "Name cannot be empty");
            return false;
        }
        snprintf(saved_name, sizeof(saved_name), "%s.txt", basename);
        macro_path(path, sizeof(path), saved_name);
        if(storage_common_exists(storage, path)) {
            furi_record_close(RECORD_STORAGE);
            snprintf(app->ui_message, sizeof(app->ui_message), "Name already exists");
            return false;
        }
    } else {
        bool found_name = false;
        for(uint16_t index = 1U; index <= 9999U; index++) {
            snprintf(saved_name, sizeof(saved_name), "live_%04u.txt", (unsigned)index);
            macro_path(path, sizeof(path), saved_name);
            if(!storage_common_exists(storage, path)) {
                found_name = true;
                break;
            }
        }
        if(!found_name) {
            furi_record_close(RECORD_STORAGE);
            snprintf(app->ui_message, sizeof(app->ui_message), "No free macro names");
            return false;
        }
    }

    File* file = storage_file_alloc(storage);
    bool saved = file && storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
    if(saved) {
        saved = storage_file_write(file, app->live_record_pending_text, app->live_record_pending_bytes) ==
                    app->live_record_pending_bytes &&
                storage_file_sync(file);
    }
    if(file) {
        const bool closed = storage_file_close(file);
        saved = saved && closed;
        storage_file_free(file);
    }
    if(!saved) storage_common_remove(storage, path);
    furi_record_close(RECORD_STORAGE);
    if(!saved) {
        snprintf(app->ui_message, sizeof(app->ui_message), "SD write failed");
        write_error_log(app, "Macro save failed");
        return false;
    }

    char display_name[MACRO_BASENAME_MAX + 1U];
    const size_t name_length = strlen(saved_name);
    const size_t base_length = name_length > 4U ? name_length - 4U : name_length;
    memcpy(display_name, saved_name, base_length);
    display_name[base_length] = '\0';
    free(app->live_record_pending_text);
    app->live_record_pending_text = NULL;
    app->live_record_pending_bytes = 0U;
    app->live_record_pending_steps = 0U;
    app->live_record_suggested_name[0] = '\0';
    app->live_record_saved = true;
    snprintf(app->live_record_saved_name, sizeof(app->live_record_saved_name), "%s", saved_name);
    app->macro_total_ms = app->live_record_elapsed_ms;
    snprintf(app->ui_message, sizeof(app->ui_message), "Saved %.24s", display_name);
    refresh_macro_files(app);
    return true;
}

static bool suggested_live_recording_name(char* output, size_t capacity) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) return false;
    char filename[MACRO_FILENAME_MAX];
    char path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    bool found = false;
    for(uint16_t index = 1U; index <= 9999U; index++) {
        snprintf(filename, sizeof(filename), "live_%04u.txt", (unsigned)index);
        macro_path(path, sizeof(path), filename);
        if(!storage_common_exists(storage, path)) {
            snprintf(output, capacity, "live_%04u", (unsigned)index);
            found = true;
            break;
        }
    }
    furi_record_close(RECORD_STORAGE);
    return found;
}

static void begin_live_recording_rename(SwitchMacroApp* app);

static bool stop_and_open_recording_keyboard(SwitchMacroApp* app, bool exit_after_save) {
    if(app->live_recording || app->live_record_full || app->live_record_limit_reached) {
        if(!finish_live_recording(app)) return false;
    }
    if(!app->live_record_pending_text) return false;
    if(!suggested_live_recording_name(
           app->live_record_suggested_name, sizeof(app->live_record_suggested_name))) {
        snprintf(app->ui_message, sizeof(app->ui_message), "No free macro names");
        return false;
    }
    app->live_exit_after_prompt = exit_after_save;
    app->ui_message[0] = '\0';
    begin_live_recording_rename(app);
    return app->live_ui_screen == LiveUiRenameRecording;
}

static void exit_live_controller(SwitchMacroApp* app) {
    app->live_exit_after_prompt = false;
    if(app->dev_fake_usb_session) {
        app->dev_fake_usb_session = false;
    } else {
        stop_live_mode(app);
    }
    app->state = RunReady;
    app->macro_ui_screen = MacroUiHome;
    refresh_macro_files(app);
}

static void finish_live_recording_save(SwitchMacroApp* app) {
    app->live_ui_screen = LiveUiRecorder;
    if(app->live_exit_after_prompt) exit_live_controller(app);
}

static void begin_live_recording_rename(SwitchMacroApp* app) {
    if(!app->live_record_suggested_name[0] &&
       !suggested_live_recording_name(
           app->live_record_suggested_name, sizeof(app->live_record_suggested_name))) {
        snprintf(app->ui_message, sizeof(app->ui_message), "No free macro names");
        return;
    }
    snprintf(app->rename_name, sizeof(app->rename_name), "%s", app->live_record_suggested_name);
    app->rename_position = (uint8_t)strlen(app->rename_name);
    app->rename_keyboard_row = 0U;
    app->rename_keyboard_column = 0U;
    app->rename_symbols = false;
    app->rename_lowercase = false;
    app->rename_cursor_selected = false;
    app->rename_target = RenameTargetRecording;
    app->ui_message[0] = '\0';
    app->live_ui_screen = LiveUiRenameRecording;
}

static void load_selected_macro(SwitchMacroApp* app) {
    app->backup_loaded = false;
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
        write_error_log(app, "Macro load failed");
    } else {
        for(uint16_t i = 0U; i < app->step_count; i++) {
            app->macro_total_ms += app->steps[i].duration_ms;
        }
        app->ui_message[0] = '\0';
    }
}

static bool macro_append_token(char* output, size_t capacity, size_t* used, const char* token) {
    if(*used >= capacity) return false;
    const int written = snprintf(output + *used, capacity - *used, "%s%s", *used ? "+" : "", token);
    if(written < 0 || (size_t)written >= capacity - *used) return false;
    *used += (size_t)written;
    return true;
}

static const char* macro_hat_token(uint8_t hat) {
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

static bool macro_format_step(char* output, size_t capacity, size_t* line_length, const MacroStep* step) {
    char controls[160] = {0};
    size_t control_length = 0U;
    for(size_t i = 0U; i < COUNT_OF(macro_edit_buttons); i++) {
        if((step->buttons & macro_edit_buttons[i].mask) &&
           !macro_append_token(controls, sizeof(controls), &control_length, macro_edit_buttons[i].name)) {
            return false;
        }
    }
    const char* hat = macro_hat_token(step->hat);
    if(hat && !macro_append_token(controls, sizeof(controls), &control_length, hat)) return false;
    const uint8_t axes[] = {step->lx, step->ly, step->rx, step->ry};
    static const char* const axis_names[] = {"LX", "LY", "RX", "RY"};
    for(size_t i = 0U; i < COUNT_OF(axes); i++) {
        if(axes[i] != 0x80U) {
            char axis[8];
            snprintf(axis, sizeof(axis), "%s:%u", axis_names[i], (unsigned)axes[i]);
            if(!macro_append_token(controls, sizeof(controls), &control_length, axis)) return false;
        }
    }

    int written;
    if(control_length == 0U && !step->state_mode) {
        written = snprintf(output, capacity, "WAIT %lu\n", (unsigned long)step->duration_ms);
    } else {
        if(control_length == 0U) snprintf(controls, sizeof(controls), "NONE");
        written = snprintf(
            output,
            capacity,
            step->state_mode ? "STATE %s %lu\n" : "%s %lu\n",
            controls,
            (unsigned long)step->duration_ms);
    }
    if(written < 0 || (size_t)written >= capacity) {
        return false;
    }
    *line_length = (size_t)written;
    return true;
}

static bool macro_save_edited_file(SwitchMacroApp* app) {
    if(!app->steps || app->step_count == 0U || app->macro_index >= app->macro_count) return false;
    static const char header[] = "# Edited on Flipper Zero\n# STATE controls duration_ms\n";
    const char* name = app->macro_files[app->macro_index].name;
    char path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 2U];
    char temp_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 8U];
    char backup_path[sizeof(MACRO_DIRECTORY) + MACRO_FILENAME_MAX + 8U];
    macro_path(path, sizeof(path), name);
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);
    snprintf(backup_path, sizeof(backup_path), "%s.bak", path);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    if(!storage) {
        snprintf(app->ui_message, sizeof(app->ui_message), "SD unavailable");
        return false;
    }
    if(storage_common_exists(storage, temp_path)) storage_common_remove(storage, temp_path);
    if(storage_common_exists(storage, backup_path) &&
       storage_common_remove(storage, backup_path) != FSE_OK) {
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Cannot clear backup");
        return false;
    }
    File* file = storage_file_alloc(storage);
    if(!file) {
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Not enough memory");
        return false;
    }
    const bool opened = storage_file_open(file, temp_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    char output_buffer[512];
    size_t buffered = sizeof(header) - 1U;
    size_t total = buffered;
    memcpy(output_buffer, header, buffered);
    bool content_valid = true;
    bool write_ok = opened;
    const char* failure_message = "Macro validation failed";
    for(uint16_t i = 0U; opened && i < app->step_count; i++) {
        char line[192];
        char validation_line[sizeof(line)];
        size_t line_length = 0U;
        if(!macro_format_step(line, sizeof(line), &line_length, &app->steps[i])) {
            content_valid = false;
            failure_message = "Macro exceeds file limit";
            break;
        }
        if(total > MACRO_FILE_MAX_BYTES || line_length > MACRO_FILE_MAX_BYTES - total) {
            content_valid = false;
            failure_message = "Macro exceeds file limit";
            break;
        }
        memcpy(validation_line, line, line_length + 1U);
        MacroStep parsed_step = {0};
        bool has_step = false;
        if(!parse_macro_line(validation_line, &parsed_step, &has_step) || !has_step ||
           parsed_step.duration_ms != app->steps[i].duration_ms) {
            content_valid = false;
            break;
        }
        if(buffered + line_length > sizeof(output_buffer)) {
            if(storage_file_write(file, output_buffer, buffered) != buffered) {
                write_ok = false;
                break;
            }
            buffered = 0U;
        }
        memcpy(output_buffer + buffered, line, line_length);
        buffered += line_length;
        total += line_length;
    }
    if(opened && content_valid && write_ok && buffered > 0U) {
        write_ok = storage_file_write(file, output_buffer, buffered) == buffered;
    }
    const bool synced = opened && content_valid && write_ok && storage_file_sync(file);
    const bool closed = storage_file_close(file);
    storage_file_free(file);
    if(!opened || !content_valid || !write_ok || !synced || !closed) {
        storage_common_remove(storage, temp_path);
        furi_record_close(RECORD_STORAGE);
        snprintf(
            app->ui_message,
            sizeof(app->ui_message),
            "%s",
            content_valid ? "SD write failed" : failure_message);
        return false;
    }

    if(storage_common_rename(storage, path, backup_path) != FSE_OK) {
        storage_common_remove(storage, temp_path);
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Cannot stage old macro");
        return false;
    }
    if(storage_common_rename(storage, temp_path, path) != FSE_OK) {
        storage_common_rename(storage, backup_path, path);
        storage_common_remove(storage, temp_path);
        furi_record_close(RECORD_STORAGE);
        snprintf(app->ui_message, sizeof(app->ui_message), "Cannot replace macro");
        return false;
    }
    storage_common_remove(storage, backup_path);
    furi_record_close(RECORD_STORAGE);

    app->macro_total_ms = 0U;
    for(uint16_t i = 0U; i < app->step_count; i++) app->macro_total_ms += app->steps[i].duration_ms;
    app->macro_editor_dirty = false;
    snprintf(app->ui_message, sizeof(app->ui_message), "Macro saved");
    return true;
}

static bool macro_editor_duplicate_step(SwitchMacroApp* app) {
    if(app->step_count >= MACRO_MAX_STEPS) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Macro is full");
        return false;
    }
    const uint16_t after = app->macro_editor_step + 1U;
    if(after < app->step_count) {
        memmove(
            &app->steps[after + 1U],
            &app->steps[after],
            sizeof(MacroStep) * (app->step_count - after));
    }
    app->steps[after] = app->steps[app->macro_editor_step];
    app->step_count++;
    app->macro_editor_step = after;
    app->macro_editor_dirty = true;
    app->ui_message[0] = '\0';
    return true;
}

static bool macro_editor_delete_step(SwitchMacroApp* app) {
    if(app->step_count <= 1U) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Keep one macro step");
        return false;
    }
    memmove(
        &app->steps[app->macro_editor_step],
        &app->steps[app->macro_editor_step + 1U],
        sizeof(MacroStep) * (app->step_count - app->macro_editor_step - 1U));
    app->step_count--;
    if(app->macro_editor_step >= app->step_count) app->macro_editor_step = app->step_count - 1U;
    app->macro_editor_dirty = true;
    app->ui_message[0] = '\0';
    return true;
}

static void select_macro_named(SwitchMacroApp* app, const char* name) {
    for(uint8_t i = 0U; i < app->macro_count; i++) {
        if(strcmp(app->macro_files[i].name, name) == 0) {
            app->macro_index = i;
            normalize_macro_list_offset(app);
            return;
        }
    }
    if(app->macro_list_mode != MacroListAll) {
        char retained_name[MACRO_FILENAME_MAX];
        snprintf(retained_name, sizeof(retained_name), "%s", name);
        app->macro_list_mode = MacroListAll;
        refresh_macro_files(app);
        for(uint8_t i = 0U; i < app->macro_count; i++) {
            if(strcmp(app->macro_files[i].name, retained_name) == 0) {
                app->macro_index = i;
                normalize_macro_list_offset(app);
                return;
            }
        }
    }
}

static bool delete_selected_macro(SwitchMacroApp* app) {
    if(app->macro_count == 0U || app->macro_index >= app->macro_count) return false;
    const char* name = app->macro_files[app->macro_index].name;
    char saved_name[MACRO_FILENAME_MAX];
    snprintf(saved_name, sizeof(saved_name), "%s", name);
    if(!move_macro_to_backup(app, saved_name)) return false;
    macro_preference_remove(app, name);
    (void)save_app_settings(app);
    refresh_macro_files(app);
    load_selected_macro(app);
    snprintf(app->ui_message, sizeof(app->ui_message), "Macro moved to Backup");
    return true;
}

static bool rename_selected_macro(SwitchMacroApp* app) {
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
    char old_name[MACRO_FILENAME_MAX];
    snprintf(old_name, sizeof(old_name), "%s", app->macro_files[app->macro_index].name);
    macro_path(old_path, sizeof(old_path), old_name);
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
    macro_preference_rename(app, old_name, new_name);
    (void)save_app_settings(app);
    refresh_macro_files(app);
    select_macro_named(app, new_name);
    load_selected_macro(app);
    snprintf(app->ui_message, sizeof(app->ui_message), "Macro renamed");
    return true;
}

static int32_t macro_worker(void* ctx);

static void begin_macro_rename(SwitchMacroApp* app) {
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
    app->rename_lowercase = false;
    app->rename_cursor_selected = false;
    app->rename_target = RenameTargetMacro;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiRename;
}

static void begin_profile_rename(SwitchMacroApp* app) {
    app->rename_profile_index = app->settings.active_profile;
    snprintf(
        app->rename_name,
        sizeof(app->rename_name),
        "%s",
        app->settings.profile_names[app->rename_profile_index]);
    app->rename_position = (uint8_t)strlen(app->rename_name);
    app->rename_keyboard_row = 0U;
    app->rename_keyboard_column = 0U;
    app->rename_symbols = false;
    app->rename_lowercase = false;
    app->rename_cursor_selected = false;
    app->rename_target = RenameTargetProfile;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiRename;
}

static bool rename_selected_profile(SwitchMacroApp* app) {
    const size_t length = strlen(app->rename_name);
    if(app->rename_profile_index >= PROFILE_COUNT || length == 0U || length > PROFILE_NAME_MAX) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Name must be 1-9 chars");
        return false;
    }
    for(uint8_t i = 0U; i < PROFILE_COUNT; i++) {
        if(i != app->rename_profile_index &&
           macro_name_compare(app->settings.profile_names[i], app->rename_name) == 0) {
            snprintf(app->ui_message, sizeof(app->ui_message), "Name already used");
            return false;
        }
    }
    char previous_name[PROFILE_NAME_MAX + 1U];
    memcpy(previous_name, app->settings.profile_names[app->rename_profile_index], sizeof(previous_name));
    memcpy(
        app->settings.profile_names[app->rename_profile_index],
        app->rename_name,
        length + 1U);
    if(!save_app_settings(app)) {
        memcpy(app->settings.profile_names[app->rename_profile_index], previous_name, sizeof(previous_name));
        snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        return false;
    }
    app->ui_message[0] = '\0';
    return true;
}

static uint8_t nearest_rename_column(const SwitchMacroApp* app, uint8_t row, uint8_t x) {
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

static void move_rename_vertical(SwitchMacroApp* app, bool up) {
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

static void move_rename_horizontal(SwitchMacroApp* app, bool right) {
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

static void activate_rename_key(SwitchMacroApp* app) {
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
    } else if(key->action == RenameKeyCase) {
        if(!app->rename_symbols) app->rename_lowercase = !app->rename_lowercase;
    } else if(key->action == RenameKeySave) {
        if(app->rename_target == RenameTargetProfile) {
            if(rename_selected_profile(app)) app->macro_ui_screen = MacroUiProfiles;
        } else if(app->rename_target == RenameTargetRecording) {
            if(save_pending_live_recording(app, app->rename_name)) {
                finish_live_recording_save(app);
            }
        } else if(app->rename_target == RenameTargetRestore) {
            (void)restore_selected_backup(app, app->rename_name, false);
        } else if(rename_selected_macro(app)) {
            app->macro_ui_screen = MacroUiList;
        }
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
        const size_t max_length = app->rename_target == RenameTargetProfile ?
                                      PROFILE_NAME_MAX : MACRO_BASENAME_MAX;
        if(length >= max_length) {
            snprintf(
                app->ui_message,
                sizeof(app->ui_message),
                app->rename_target == RenameTargetProfile ? "Max 9 characters" : "Name is too long");
            return;
        }
        char value = key->action == RenameKeySpace ? ' ' : key->value;
        if(!app->rename_symbols && app->rename_lowercase && isalpha((unsigned char)value)) {
            value = (char)tolower((unsigned char)value);
        }
        const size_t position = app->rename_position > length ? length : app->rename_position;
        memmove(app->rename_name + position + 1U, app->rename_name + position, length - position + 1U);
        app->rename_name[position] = value;
        app->rename_position = position + 1U;
        app->ui_message[0] = '\0';
    }
}

static void update_macro_playback_total(SwitchMacroApp* app) {
    uint64_t total = 0U;
    for(uint16_t i = 0U; i < app->step_count; i++) {
        total += playback_duration_ms(app->steps[i].duration_ms, app->macro_speed_index);
    }
    app->macro_playback_total_ms = total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static void start_macro_playback(SwitchMacroApp* app, InputKey initiating_key) {
    if(app->load_error != MacroLoadOk || app->worker) return;
    if(!app->connected && !app->dev_fake_usb) {
        request_usb_error_flash(app, initiating_key);
        return;
    }
    if(!app->backup_loaded) {
        if(app->macro_index >= app->macro_count) return;
        const char* macro_name = app->macro_files[app->macro_index].name;
        MacroPreference* preference = macro_preference_get(app, macro_name);
        if(preference) {
        if(preference->profile_index < PROFILE_COUNT) {
            app->settings.active_profile = preference->profile_index;
        }
        if(app->settings.recent_clock == UINT32_MAX) {
            app->settings.recent_clock = 0U;
            for(uint8_t i = 0U; i < MACRO_PREF_COUNT; i++) {
                app->settings.macros[i].recent_stamp = 0U;
            }
        }
        preference->recent_stamp = ++app->settings.recent_clock;
        (void)save_app_settings(app);
        }
    }
    app->stop_requested = false;
    app->completed_steps = 0U;
    app->playback_step = 0U;
    app->macro_elapsed_ms = 0U;
    app->macro_cycles = 0U;
    app->macro_paused = false;
    app->macro_speed_index = active_profile(app)->playback_speed_index;
    if(app->macro_repeat_count == 0U) app->macro_repeat_count = 1U;
    update_macro_playback_total(app);
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

static bool simulator_adjust_stick(SwitchMacroApp* app, InputKey key) {
    if(!app->connected) {
        app->simulator_error = !app->dev_fake_usb;
        return false;
    }

    SwitchReport next = app->simulator_report;
    uint8_t* horizontal = app->simulator_selection == SimulatorControlLeftStick ? &next.lx : &next.rx;
    uint8_t* vertical = app->simulator_selection == SimulatorControlLeftStick ? &next.ly : &next.ry;
    const int16_t step = active_profile(app)->simulator_step;
    uint8_t* axis = NULL;
    int16_t delta = 0;
    if(key == InputKeyUp) {
        axis = vertical;
        delta = -step;
    } else if(key == InputKeyDown) {
        axis = vertical;
        delta = step;
    } else if(key == InputKeyLeft) {
        axis = horizontal;
        delta = -step;
    } else if(key == InputKeyRight) {
        axis = horizontal;
        delta = step;
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

static void handle_simulator_input(SwitchMacroApp* app, InputKey key, bool repeat) {
    if(app->simulator_error) {
        app->simulator_error = false;
        return;
    }
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
            app->simulator_error = !app->dev_fake_usb;
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

static void release_simulator_button(SwitchMacroApp* app, InputKey key) {
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
        app->simulator_error = app->connected || !app->dev_fake_usb;
    }
}

static uint8_t cycle_setting_value(uint8_t current, const uint8_t* values, size_t count, bool right) {
    size_t index = 0U;
    for(size_t i = 0U; i < count; i++) {
        if(values[i] == current) {
            index = i;
            break;
        }
    }
    index = right ? (index + 1U) % count : (index + count - 1U) % count;
    return values[index];
}

static void enter_stick_calibration(SwitchMacroApp* app) {
    app->calibration_selection = 0U;
    app->calibration_right_stick = false;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiCalibration;
}

static void handle_profile_settings(SwitchMacroApp* app, InputKey key) {
    GameProfile* profile = active_profile(app);
    if(key == InputKeyUp) {
        app->profile_selection = (app->profile_selection + 6U) % 7U;
    } else if(key == InputKeyDown) {
        app->profile_selection = (app->profile_selection + 1U) % 7U;
    } else if((key == InputKeyLeft || key == InputKeyRight) && app->profile_selection < 5U) {
        const bool right = key == InputKeyRight;
        if(app->profile_selection == 0U) {
            app->settings.active_profile = right ?
                                               (app->settings.active_profile + 1U) % PROFILE_COUNT :
                                               (app->settings.active_profile + PROFILE_COUNT - 1U) % PROFILE_COUNT;
        } else if(app->profile_selection == 1U) {
            if(right) {
                if(profile->left_deadzone < 32U) profile->left_deadzone++;
                if(profile->right_deadzone < 32U) profile->right_deadzone++;
            } else {
                if(profile->left_deadzone > 0U) profile->left_deadzone--;
                if(profile->right_deadzone > 0U) profile->right_deadzone--;
            }
        } else if(app->profile_selection == 2U) {
            if(right) {
                if(profile->left_gain_percent < 200U) profile->left_gain_percent++;
                if(profile->right_gain_percent < 200U) profile->right_gain_percent++;
            } else {
                if(profile->left_gain_percent > 50U) profile->left_gain_percent--;
                if(profile->right_gain_percent > 50U) profile->right_gain_percent--;
            }
        } else if(app->profile_selection == 3U) {
            profile->simulator_step = cycle_setting_value(
                profile->simulator_step, profile_simulator_steps, sizeof(profile_simulator_steps), right);
        } else {
            profile->playback_speed_index = right ?
                                                (profile->playback_speed_index + 1U) % sizeof(playback_speeds) :
                                                (profile->playback_speed_index + sizeof(playback_speeds) - 1U) %
                                                    sizeof(playback_speeds);
        }
    } else if(key == InputKeyOk && app->profile_selection == 0U) {
        app->settings.active_profile = (app->settings.active_profile + 1U) % PROFILE_COUNT;
    } else if(key == InputKeyOk && app->profile_selection == 5U) {
        enter_stick_calibration(app);
    } else if(key == InputKeyOk && app->profile_selection == 6U) {
        begin_profile_rename(app);
    } else if(key == InputKeyBack) {
        if(!save_app_settings(app)) {
            snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        } else {
            app->ui_message[0] = '\0';
        }
        app->macro_ui_screen = MacroUiHome;
    }
}

static void handle_stick_calibration(SwitchMacroApp* app, InputKey key) {
    GameProfile* profile = active_profile(app);
    uint8_t* deadzone = app->calibration_right_stick ?
                            &profile->right_deadzone : &profile->left_deadzone;
    uint8_t* gain = app->calibration_right_stick ?
                        &profile->right_gain_percent : &profile->left_gain_percent;
    if(key == InputKeyUp) {
        app->calibration_selection = (app->calibration_selection + 4U) % 5U;
    } else if(key == InputKeyDown) {
        app->calibration_selection = (app->calibration_selection + 1U) % 5U;
    } else if((key == InputKeyLeft || key == InputKeyRight) && app->calibration_selection == 0U) {
        app->calibration_right_stick = key == InputKeyRight;
    } else if((key == InputKeyLeft || key == InputKeyRight) && app->calibration_selection == 1U) {
        const bool increase = key == InputKeyRight;
        if(increase && *deadzone < 32U) (*deadzone)++;
        else if(!increase && *deadzone > 0U) (*deadzone)--;
    } else if((key == InputKeyLeft || key == InputKeyRight) && app->calibration_selection == 2U) {
        const bool increase = key == InputKeyRight;
        if(increase && *gain < 200U) (*gain)++;
        else if(!increase && *gain > 50U) (*gain)--;
    } else if(key == InputKeyOk && app->calibration_selection == 3U) {
        if(save_app_settings(app)) {
            app->ui_message[0] = '\0';
            app->macro_ui_screen = MacroUiProfiles;
        } else {
            snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        }
    } else if(key == InputKeyOk && app->calibration_selection == 4U) {
        const uint8_t previous_deadzone = *deadzone;
        const uint8_t previous_gain = *gain;
        *deadzone = profile_deadzones[0];
        *gain = profile_gains[0];
        if(save_app_settings(app)) {
            app->ui_message[0] = '\0';
        } else {
            *deadzone = previous_deadzone;
            *gain = previous_gain;
            snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        }
    } else if(key == InputKeyBack) {
        if(!save_app_settings(app)) {
            snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        } else {
            app->ui_message[0] = '\0';
        }
        app->macro_ui_screen = MacroUiProfiles;
    }
}

static void begin_macro_editor(SwitchMacroApp* app) {
    if(app->load_error != MacroLoadOk || !app->steps || !app->step_count) return;
    app->macro_editor_step = 0U;
    app->macro_editor_selection = 0U;
    app->macro_editor_button = 0U;
    app->macro_editor_dirty = false;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiEditor;
}

static void begin_macro_profile_assignment(SwitchMacroApp* app) {
    if(app->macro_index >= app->macro_count) return;
    const MacroPreference* preference =
        macro_preference_find_const(app, app->macro_files[app->macro_index].name);
    app->macro_profile_selection =
        preference && preference->profile_index < PROFILE_COUNT ? preference->profile_index + 1U : 0U;
    app->ui_message[0] = '\0';
    app->macro_ui_screen = MacroUiAssignProfile;
}

static bool save_macro_profile_assignment(SwitchMacroApp* app) {
    if(app->macro_index >= app->macro_count) return false;
    MacroPreference* preference = macro_preference_get(app, app->macro_files[app->macro_index].name);
    if(!preference) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Preference list full");
        return false;
    }
    const uint8_t previous_profile = preference->profile_index;
    preference->profile_index = app->macro_profile_selection == 0U ?
                                    MACRO_NO_PROFILE : app->macro_profile_selection - 1U;
    if(!save_app_settings(app)) {
        preference->profile_index = previous_profile;
        snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        return false;
    }
    app->ui_message[0] = '\0';
    return true;
}

static void toggle_macro_favorite(SwitchMacroApp* app) {
    if(app->macro_index >= app->macro_count) return;
    MacroPreference* preference = macro_preference_get(app, app->macro_files[app->macro_index].name);
    if(!preference) {
        snprintf(app->ui_message, sizeof(app->ui_message), "Preference list full");
        return;
    }
    const uint8_t previous = preference->favorite;
    preference->favorite = !preference->favorite;
    if(!save_app_settings(app)) {
        preference->favorite = previous;
        snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
        return;
    }
    const bool favorite = preference->favorite != 0U;
    app->macro_ui_screen = MacroUiList;
    refresh_macro_files(app);
    load_selected_macro(app);
    snprintf(app->ui_message, sizeof(app->ui_message), "%s", favorite ? "Added to favorites" : "Removed from favorites");
}

static void recalculate_macro_duration(SwitchMacroApp* app) {
    app->macro_total_ms = 0U;
    for(uint16_t i = 0U; i < app->step_count; i++) app->macro_total_ms += app->steps[i].duration_ms;
}

static bool edit_current_step_duration(SwitchMacroApp* app, bool increase) {
    MacroStep* step = &app->steps[app->macro_editor_step];
    if(increase) {
        if(step->duration_ms >= MACRO_MAX_STEP_MS) return false;
        const uint32_t next = step->duration_ms + 100U;
        step->duration_ms = next > MACRO_MAX_STEP_MS ? MACRO_MAX_STEP_MS : next;
    } else {
        if(step->duration_ms <= 1U) return false;
        step->duration_ms = step->duration_ms > 100U ? step->duration_ms - 100U : 1U;
    }
    app->macro_editor_dirty = true;
    app->ui_message[0] = '\0';
    recalculate_macro_duration(app);
    return true;
}

static void handle_macro_editor_input(SwitchMacroApp* app, InputKey key) {
    if(!app->steps || app->step_count == 0U) return;
    MacroStep* step = &app->steps[app->macro_editor_step];
    const bool left = key == InputKeyLeft;
    const bool right = key == InputKeyRight;
    if(key == InputKeyUp) {
        app->macro_editor_selection = (app->macro_editor_selection + 7U) % 8U;
    } else if(key == InputKeyDown) {
        app->macro_editor_selection = (app->macro_editor_selection + 1U) % 8U;
    } else if((left || right) && app->macro_editor_selection == 0U) {
        if(left && app->macro_editor_step > 0U) app->macro_editor_step--;
        else if(right && app->macro_editor_step + 1U < app->step_count) app->macro_editor_step++;
    } else if((left || right) && app->macro_editor_selection == 1U) {
        (void)edit_current_step_duration(app, right);
    } else if((left || right) && app->macro_editor_selection == 2U) {
        const size_t button_count = COUNT_OF(macro_edit_buttons);
        app->macro_editor_button = (uint8_t)(right ?
                                                  (app->macro_editor_button + 1U) % button_count :
                                                  (app->macro_editor_button + button_count - 1U) % button_count);
    } else if((left || right || key == InputKeyOk) && app->macro_editor_selection == 3U) {
        step->state_mode = !step->state_mode;
        app->macro_editor_dirty = true;
        app->ui_message[0] = '\0';
    } else if((left || right) && app->macro_editor_selection == 4U) {
        size_t hat_index = 0U;
        for(size_t i = 0U; i < COUNT_OF(macro_edit_hats); i++) {
            if(macro_edit_hats[i] == step->hat) {
                hat_index = i;
                break;
            }
        }
        const size_t count = COUNT_OF(macro_edit_hats);
        hat_index = right ? (hat_index + 1U) % count : (hat_index + count - 1U) % count;
        step->hat = macro_edit_hats[hat_index];
        app->macro_editor_dirty = true;
        app->ui_message[0] = '\0';
    } else if(key == InputKeyOk && app->macro_editor_selection == 2U) {
        step->buttons ^= macro_edit_buttons[app->macro_editor_button].mask;
        app->macro_editor_dirty = true;
        app->ui_message[0] = '\0';
    } else if(key == InputKeyOk && app->macro_editor_selection == 5U) {
        (void)macro_editor_duplicate_step(app);
        recalculate_macro_duration(app);
    } else if(key == InputKeyOk && app->macro_editor_selection == 6U) {
        (void)macro_editor_delete_step(app);
        recalculate_macro_duration(app);
    } else if(key == InputKeyOk && app->macro_editor_selection == 7U) {
        if(macro_save_edited_file(app)) {
            app->macro_ui_screen = MacroUiActions;
            app->ui_message[0] = '\0';
        }
    } else if(key == InputKeyBack) {
        if(!app->macro_editor_dirty || macro_save_edited_file(app)) {
            app->macro_ui_screen = MacroUiActions;
            app->ui_message[0] = '\0';
        }
    }
}

static void change_macro_list_mode(SwitchMacroApp* app, bool right) {
    if(right) {
        if(app->macro_list_mode == MacroListRecent) {
            app->backup_from_live = false;
            app->backup_from_macro_list = true;
            app->backup_return_list_mode = app->macro_list_mode;
            app->ui_message[0] = '\0';
            refresh_backups(app);
            app->backup_index = 0U;
            app->backup_offset = 0U;
            app->macro_ui_screen = MacroUiBackupList;
            return;
        }
        app->macro_list_mode = (MacroListMode)((app->macro_list_mode + 1U) % 3U);
    } else {
        app->macro_list_mode = (MacroListMode)((app->macro_list_mode + 2U) % 3U);
    }
    refresh_macro_files(app);
    load_selected_macro(app);
}

static void release_backup_macro(SwitchMacroApp* app) {
    if(app->backup_loaded && app->steps) {
        free(app->steps);
        app->steps = NULL;
        app->step_count = 0U;
        app->macro_total_ms = 0U;
        app->load_error = MacroLoadMissing;
    }
    app->backup_loaded = false;
    app->backup_preview_playing = false;
}

static void leave_backup_list(SwitchMacroApp* app) {
    app->ui_message[0] = '\0';
    if(app->backup_from_live) {
        app->backup_from_live = false;
        app->state = RunLive;
        app->live_ui_screen = LiveUiRecorder;
        app->live_recorder_selection = 3U;
    } else if(app->backup_from_macro_list) {
        app->backup_from_macro_list = false;
        app->macro_list_mode = app->backup_return_list_mode;
        app->macro_ui_screen = MacroUiList;
        refresh_macro_files(app);
        load_selected_macro(app);
    } else {
        app->macro_ui_screen = MacroUiHome;
    }
}

static void handle_backup_ui_input(SwitchMacroApp* app, InputKey key) {
    if(app->macro_ui_screen == MacroUiBackupList) {
        if(app->backup_count == 0U) {
            if(key == InputKeyBack || key == InputKeyOk) leave_backup_list(app);
            return;
        }
        if(key == InputKeyUp) {
            app->backup_index = app->backup_index == 0U ? app->backup_count - 1U : app->backup_index - 1U;
        } else if(key == InputKeyDown) {
            app->backup_index = (uint8_t)((app->backup_index + 1U) % app->backup_count);
        } else if(key == InputKeyOk) {
            app->backup_action_index = 0U;
            app->backup_action_offset = 0U;
            app->backup_loaded = false;
            app->macro_ui_screen = MacroUiBackupDetails;
        } else if(key == InputKeyBack) {
            leave_backup_list(app);
        }
        app->backup_offset = app->backup_index >= BACKUP_LIST_VISIBLE ?
                                 app->backup_index - BACKUP_LIST_VISIBLE + 1U :
                                 0U;
    } else if(app->macro_ui_screen == MacroUiBackupDetails) {
        static const uint8_t action_count = 6U;
        if(key == InputKeyUp) {
            app->backup_action_index = app->backup_action_index == 0U ?
                                           action_count - 1U : app->backup_action_index - 1U;
        } else if(key == InputKeyDown) {
            app->backup_action_index = (uint8_t)((app->backup_action_index + 1U) % action_count);
        } else if(key == InputKeyOk && app->backup_index < app->backup_count) {
            if(app->backup_action_index == 0U) {
                if(load_selected_backup(app)) {
                    app->backup_preview_playing = true;
                    app->backup_preview_step_started = furi_get_tick();
                    app->macro_ui_screen = MacroUiBackupPreview;
                }
            } else if(app->backup_action_index == 1U) {
                if(load_selected_backup(app)) {
                    app->backup_preview_playing = false;
                    app->macro_loop = false;
                    app->macro_repeat_count = 1U;
                    app->macro_ui_screen = MacroUiBackupPreview;
                    start_macro_playback(app, key);
                }
            } else if(app->backup_action_index == 2U) {
                begin_backup_restore(app);
            } else if(app->backup_action_index == 3U) {
                begin_backup_rename(app);
            } else if(app->backup_action_index == 4U) {
                app->backup_conflict_selection = 0U;
                app->macro_ui_screen = MacroUiBackupDeleteConfirm;
            } else {
                release_backup_macro(app);
                app->macro_ui_screen = MacroUiBackupList;
            }
        } else if(key == InputKeyBack) {
            release_backup_macro(app);
            app->macro_ui_screen = MacroUiBackupList;
        }
        if(app->backup_action_index < app->backup_action_offset) {
            app->backup_action_offset = app->backup_action_index;
        } else if(app->backup_action_index >= app->backup_action_offset + BACKUP_ACTION_VISIBLE) {
            app->backup_action_offset = app->backup_action_index - BACKUP_ACTION_VISIBLE + 1U;
        }
    } else if(app->macro_ui_screen == MacroUiBackupPreview) {
        if(key == InputKeyOk) {
            app->backup_preview_playing = !app->backup_preview_playing;
            app->backup_preview_step_started = furi_get_tick();
        } else if(key == InputKeyBack) {
            release_backup_macro(app);
            app->macro_ui_screen = MacroUiBackupDetails;
        } else if(key == InputKeyLeft || key == InputKeyUp) {
            app->backup_preview_playing = false;
            if(app->backup_preview_step > 0U) app->backup_preview_step--;
            app->backup_preview_step_started = furi_get_tick();
        } else if(key == InputKeyRight || key == InputKeyDown) {
            app->backup_preview_playing = false;
            if(app->backup_preview_step + 1U < app->step_count) app->backup_preview_step++;
            else app->backup_preview_step = 0U;
            app->backup_preview_step_started = furi_get_tick();
        }
    } else if(app->macro_ui_screen == MacroUiBackupConflict) {
        if(key == InputKeyUp) {
            app->backup_conflict_selection = (app->backup_conflict_selection + 3U) % 4U;
        } else if(key == InputKeyDown) {
            app->backup_conflict_selection = (app->backup_conflict_selection + 1U) % 4U;
        } else if(key == InputKeyBack) {
            release_backup_macro(app);
            app->macro_ui_screen = MacroUiBackupDetails;
        } else if(key == InputKeyOk) {
            if(app->backup_conflict_selection == 0U) {
                (void)restore_selected_backup(app, app->backup_restore_name, true);
            } else if(app->backup_conflict_selection == 1U) {
                begin_backup_rename(app);
            } else if(app->backup_conflict_selection == 2U) {
                (void)restore_backup_with_suffix(app);
            } else {
                release_backup_macro(app);
                app->macro_ui_screen = MacroUiBackupDetails;
            }
        }
    } else if(app->macro_ui_screen == MacroUiBackupDeleteConfirm) {
        if(key == InputKeyUp || key == InputKeyDown) {
            app->backup_conflict_selection ^= 1U;
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiBackupDetails;
        } else if(key == InputKeyOk) {
            if(app->backup_conflict_selection == 0U) {
                delete_selected_backup(app);
            } else {
                app->macro_ui_screen = MacroUiBackupDetails;
            }
        }
    }
}

static void handle_macro_ui_input(SwitchMacroApp* app, InputKey key, bool* running) {
    if(app->ui_message[0]) {
        app->ui_message[0] = '\0';
        return;
    }
    if(app->macro_ui_screen == MacroUiHome) {
        if(key == InputKeyUp) {
            app->home_index = (app->home_index + 4U) % 5U;
        } else if(key == InputKeyDown) {
            app->home_index = (app->home_index + 1U) % 5U;
        } else if(key == InputKeyOk && app->home_index == 0U) {
            app->macro_list_mode = MacroListAll;
            refresh_macro_files(app);
            if(app->live_record_saved) select_macro_named(app, app->live_record_saved_name);
            load_selected_macro(app);
            app->macro_ui_screen = MacroUiList;
        } else if(key == InputKeyOk && app->home_index == 1U) {
            app->ui_message[0] = '\0';
            if(app->connected) {
                if(!start_live_mode(app)) write_error_log(app, "Live controller start failed");
            } else if(app->dev_fake_usb) {
                app->dev_fake_usb_session = true;
                app->live_ui_screen = LiveUiRecorder;
                app->live_controller_return_screen = LiveUiRecorder;
                app->state = RunLive;
            } else {
                request_usb_error_flash(app, key);
            }
        } else if(key == InputKeyOk && app->home_index == 2U) {
            app->ui_message[0] = '\0';
            if(app->connected || app->dev_fake_usb) {
                app->simulator_report = neutral_gamepad();
                app->simulator_selection = SimulatorControlA;
                app->simulator_held_control = SIMULATOR_NO_CONTROL;
                app->simulator_error = false;
                app->simulator_stick_active = false;
                app->ui_message[0] = '\0';
                app->macro_ui_screen = MacroUiSimulator;
            } else {
                request_usb_error_flash(app, key);
            }
        } else if(key == InputKeyOk && app->home_index == 3U) {
            app->profile_selection = 0U;
            app->macro_ui_screen = MacroUiProfiles;
        } else if(key == InputKeyOk) {
            app->options_selection = 0U;
            app->macro_ui_screen = MacroUiOptions;
        } else if(key == InputKeyBack) {
            app->exit_selection = 0U;
            app->macro_ui_screen = MacroUiExitConfirm;
        }
    } else if(app->macro_ui_screen == MacroUiExitConfirm) {
        if(key == InputKeyBack) {
            *running = false;
        } else if(key == InputKeyUp || key == InputKeyDown) {
            app->exit_selection = (app->exit_selection + 1U) % 2U;
        } else if(key == InputKeyOk) {
            if(app->exit_selection == 1U) {
                *running = false;
            } else {
                app->macro_ui_screen = MacroUiHome;
            }
        }
    } else if(app->macro_ui_screen == MacroUiOptions) {
        if(key == InputKeyUp) {
            app->options_selection = (app->options_selection + 7U) % 8U;
        } else if(key == InputKeyDown) {
            app->options_selection = (app->options_selection + 1U) % 8U;
        } else if(key == InputKeyLeft || key == InputKeyRight || key == InputKeyOk) {
            const bool advance = key != InputKeyLeft;
            if(app->options_selection == 0U || app->options_selection == 1U) {
                uint8_t* setting = app->options_selection == 0U ?
                                       &app->settings.language :
                                       &app->settings.controller_layout;
                const uint8_t count = app->options_selection == 0U ? UiLanguageCount : 3U;
                const uint8_t previous = *setting;
                *setting = advance ? (uint8_t)((previous + 1U) % count) :
                                     (previous == 0U ? count - 1U : previous - 1U);
                if(app->options_selection == 0U) ui_language = (UiLanguage)*setting;
                if(save_app_settings(app)) {
                    app->ui_message[0] = '\0';
                } else {
                    *setting = previous;
                    if(app->options_selection == 0U) ui_language = (UiLanguage)previous;
                    snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
                }
            } else if(app->options_selection == 2U) {
                const uint8_t previous = app->settings.keep_screen_on;
                app->settings.keep_screen_on = !app->settings.keep_screen_on;
                if(save_app_settings(app)) {
                    set_screen_awake(app, app->settings.keep_screen_on);
                    app->ui_message[0] = '\0';
                } else {
                    app->settings.keep_screen_on = previous;
                    snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
                }
            } else if(app->options_selection == 3U) {
                const uint8_t previous = app->settings.sound_enabled;
                app->settings.sound_enabled = !app->settings.sound_enabled;
                if(save_app_settings(app)) {
                    app->ui_message[0] = '\0';
                } else {
                    app->settings.sound_enabled = previous;
                    snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
                }
            } else if(app->options_selection == 4U) {
                const uint8_t previous = app->settings.recording_limit_index;
                app->settings.recording_limit_index = (uint8_t)(
                    (previous + (advance ? 1U : RECORDING_LIMIT_COUNT - 1U)) % RECORDING_LIMIT_COUNT);
                if(save_app_settings(app)) {
                    app->ui_message[0] = '\0';
                } else {
                    app->settings.recording_limit_index = previous;
                    snprintf(app->ui_message, sizeof(app->ui_message), "Settings save failed");
                }
            } else if(app->options_selection == 5U) {
                const bool enabled = !app->dev_fake_usb;
                if(set_dev_fake_usb_mode(app, enabled)) {
                    app->ui_message[0] = '\0';
                } else {
                    snprintf(app->ui_message, sizeof(app->ui_message), "USB mode change failed");
                    write_error_log(app, "USB interface switch failed");
                }
            } else if(app->options_selection == 6U) {
                app->error_logging_enabled = !app->error_logging_enabled;
                app->ui_message[0] = '\0';
            } else if(key == InputKeyOk) {
                app->ui_message[0] = '\0';
                app->info_scroll = 0U;
                app->macro_ui_screen = MacroUiInfo;
            }
        } else if(key == InputKeyBack) {
            app->ui_message[0] = '\0';
            app->macro_ui_screen = MacroUiHome;
        }
    } else if(app->macro_ui_screen == MacroUiInfo) {
        const uint8_t max_scroll = INFO_LINE_COUNT - INFO_VISIBLE_ROWS;
        if(key == InputKeyUp && app->info_scroll > 0U) {
            app->info_scroll--;
        } else if(key == InputKeyDown && app->info_scroll < max_scroll) {
            app->info_scroll++;
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiOptions;
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
            app->macro_detail_selection = 2U;
            app->macro_repeat_count = 1U;
            load_selected_macro(app);
        } else if(key == InputKeyLeft || key == InputKeyRight) {
            change_macro_list_mode(app, key == InputKeyRight);
        } else if(key == InputKeyOk) {
            if(app->load_error == MacroLoadOk) {
                app->macro_detail_selection = 2U;
                if(app->macro_repeat_count == 0U) app->macro_repeat_count = 1U;
                app->macro_ui_screen = MacroUiDetails;
            }
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiHome;
        }
    } else if(app->macro_ui_screen == MacroUiDetails) {
        if(key == InputKeyUp) {
            app->macro_detail_selection = (app->macro_detail_selection + 3U) % 4U;
        } else if(key == InputKeyDown) {
            app->macro_detail_selection = (app->macro_detail_selection + 1U) % 4U;
        } else if(key == InputKeyOk) {
            if(app->macro_detail_selection == 0U) {
                app->macro_loop = !app->macro_loop;
            } else if(app->macro_detail_selection == 2U) {
                app->macro_play_selection = 0U;
                app->macro_ui_screen = MacroUiPlay;
            } else if(app->macro_detail_selection == 3U) {
                app->macro_action_index = 0U;
                app->macro_ui_screen = MacroUiActions;
            }
        } else if((key == InputKeyLeft || key == InputKeyRight) &&
                  app->macro_detail_selection == 0U) {
            app->macro_loop = key == InputKeyRight;
        } else if((key == InputKeyLeft || key == InputKeyRight) &&
                  app->macro_detail_selection == 1U) {
            size_t index = 0U;
            for(size_t i = 0U; i < sizeof(macro_repeat_choices); i++) {
                if(macro_repeat_choices[i] == app->macro_repeat_count) index = i;
            }
            index = key == InputKeyRight ? (index + 1U) % sizeof(macro_repeat_choices) :
                                           (index + sizeof(macro_repeat_choices) - 1U) %
                                               sizeof(macro_repeat_choices);
            app->macro_repeat_count = macro_repeat_choices[index];
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiList;
        }
    } else if(app->macro_ui_screen == MacroUiPlay) {
        if(key == InputKeyUp || key == InputKeyDown) {
            app->macro_play_selection = (app->macro_play_selection + 1U) % 2U;
        } else if(key == InputKeyOk) {
            if(app->macro_play_selection == 0U) {
                start_macro_playback(app, key);
            } else {
                app->macro_view_step = 0U;
                app->macro_ui_screen = MacroUiViewController;
            }
        } else if(key == InputKeyBack || key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiDetails;
        }
    } else if(app->macro_ui_screen == MacroUiViewController) {
        if(key == InputKeyOk) {
            start_macro_playback(app, key);
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiPlay;
        } else if(key == InputKeyLeft || key == InputKeyUp) {
            if(app->macro_view_step > 0U) app->macro_view_step--;
        } else if(key == InputKeyRight || key == InputKeyDown) {
            if(app->macro_view_step + 1U < app->step_count) app->macro_view_step++;
        }
    } else if(app->macro_ui_screen == MacroUiActions) {
        if(key == InputKeyUp) {
            app->macro_action_index = (app->macro_action_index + 5U) % 6U;
        } else if(key == InputKeyDown) {
            app->macro_action_index = (app->macro_action_index + 1U) % 6U;
        } else if(key == InputKeyBack || key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiDetails;
        } else if(key == InputKeyOk) {
            if(app->macro_action_index == 0U) {
                app->macro_preview_step = 0U;
                app->macro_ui_screen = MacroUiPreview;
            } else if(app->macro_action_index == 1U) {
                begin_macro_editor(app);
            } else if(app->macro_action_index == 2U) {
                begin_macro_profile_assignment(app);
            } else if(app->macro_action_index == 3U) {
                toggle_macro_favorite(app);
            } else if(app->macro_action_index == 4U) {
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
            app->macro_ui_screen = app->rename_target == RenameTargetProfile ?
                                       MacroUiProfiles :
                                   app->rename_target == RenameTargetRestore ?
                                       MacroUiBackupDetails : MacroUiActions;
        }
    } else if(app->macro_ui_screen == MacroUiDeleteConfirm) {
        if(key == InputKeyOk) {
            app->macro_ui_screen = MacroUiList;
            delete_selected_macro(app);
        } else if(key == InputKeyBack || key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiActions;
        }
    } else if(app->macro_ui_screen == MacroUiPreview) {
        if(key == InputKeyLeft || key == InputKeyUp) {
            if(app->macro_preview_step > 0U) app->macro_preview_step--;
        } else if(key == InputKeyRight || key == InputKeyDown || key == InputKeyOk) {
            if(app->macro_preview_step + 1U < app->step_count) app->macro_preview_step++;
        } else if(key == InputKeyBack) {
            app->macro_ui_screen = MacroUiActions;
        }
    } else if(app->macro_ui_screen == MacroUiEditor) {
        handle_macro_editor_input(app, key);
    } else if(app->macro_ui_screen == MacroUiAssignProfile) {
        if(key == InputKeyUp) {
            app->macro_profile_selection = (app->macro_profile_selection + PROFILE_COUNT) % (PROFILE_COUNT + 1U);
        } else if(key == InputKeyDown) {
            app->macro_profile_selection = (app->macro_profile_selection + 1U) % (PROFILE_COUNT + 1U);
        } else if(key == InputKeyOk) {
            if(save_macro_profile_assignment(app)) app->macro_ui_screen = MacroUiActions;
        } else if(key == InputKeyBack || key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiActions;
        }
    } else if(app->macro_ui_screen == MacroUiSimulator) {
        handle_simulator_input(app, key, false);
    } else if(app->macro_ui_screen == MacroUiProfiles) {
        handle_profile_settings(app, key);
    } else if(app->macro_ui_screen == MacroUiCalibration) {
        handle_stick_calibration(app, key);
    } else if(app->macro_ui_screen == MacroUiBackupList ||
              app->macro_ui_screen == MacroUiBackupDetails ||
              app->macro_ui_screen == MacroUiBackupPreview ||
              app->macro_ui_screen == MacroUiBackupConflict ||
              app->macro_ui_screen == MacroUiBackupDeleteConfirm) {
        handle_backup_ui_input(app, key);
    }
}

static void handle_input(SwitchMacroApp* app, InputKey key, bool* running) {
    if(app->state == RunReady) {
        handle_macro_ui_input(app, key, running);
    } else if(app->state == RunSending) {
        if(key == InputKeyBack) {
            app->stop_requested = true;
        } else if(key == InputKeyOk) {
            if(app->macro_ui_screen != MacroUiPlay || app->macro_play_selection == 0U) {
                app->macro_paused = !app->macro_paused;
                view_port_update(app->view_port);
            } else {
                app->macro_ui_screen = MacroUiViewController;
                view_port_update(app->view_port);
            }
        } else if(app->macro_ui_screen == MacroUiPlay &&
                  (key == InputKeyUp || key == InputKeyDown)) {
            app->macro_play_selection = (app->macro_play_selection + 1U) % 2U;
            view_port_update(app->view_port);
        } else if(app->macro_ui_screen == MacroUiViewController && key == InputKeyLeft) {
            app->macro_ui_screen = MacroUiPlay;
            app->macro_play_selection = 0U;
            view_port_update(app->view_port);
        }
    } else if(app->state == RunLive) {
        if(app->ui_message[0]) {
            app->ui_message[0] = '\0';
            return;
        }
        if(app->live_ui_screen == LiveUiRenameRecording) {
            if(key == InputKeyUp || key == InputKeyDown) {
                move_rename_vertical(app, key == InputKeyUp);
            } else if(key == InputKeyLeft) {
                move_rename_horizontal(app, false);
            } else if(key == InputKeyRight) {
                move_rename_horizontal(app, true);
            } else if(key == InputKeyOk) {
                activate_rename_key(app);
            } else if(key == InputKeyBack) {
                app->live_exit_after_prompt = false;
                app->live_ui_screen = LiveUiRecorder;
            }
        } else if(app->live_ui_screen == LiveUiRecorder) {
            if(key == InputKeyUp || key == InputKeyDown) {
                app->live_recorder_selection = (uint8_t)((app->live_recorder_selection +
                                                          (key == InputKeyDown ? 1U : 3U)) % 4U);
            } else if(key == InputKeyOk) {
                if(app->live_recorder_selection == 2U) {
                    app->live_controller_return_screen = LiveUiRecorder;
                    app->live_ui_screen = LiveUiController;
                } else if(app->live_recorder_selection == 1U) {
                    if(!toggle_live_record_pause(app)) {
                        snprintf(
                            app->ui_message,
                            sizeof(app->ui_message),
                            app->live_recording ? "Pause unavailable" : "Start recording first");
                    } else {
                        app->ui_message[0] = '\0';
                    }
                } else if(app->live_recorder_selection == 3U) {
                    if(app->live_recording || app->live_record_full || app->live_record_limit_reached ||
                       app->live_record_pending_text) {
                        (void)stop_and_open_recording_keyboard(app, false);
                    } else {
                        app->backup_from_live = true;
                        app->backup_from_macro_list = false;
                        app->state = RunReady;
                        refresh_backups(app);
                        app->backup_index = 0U;
                        app->backup_offset = 0U;
                        app->macro_ui_screen = MacroUiBackupList;
                    }
                } else if(app->live_record_pending_text) {
                    (void)stop_and_open_recording_keyboard(app, false);
                } else if(app->live_recording || app->live_record_full || app->live_record_limit_reached) {
                    (void)stop_and_open_recording_keyboard(app, false);
                } else if(!start_live_recording(app)) {
                    snprintf(app->ui_message, sizeof(app->ui_message), "Record unavailable");
                }
            } else if(key == InputKeyBack) {
                if(app->live_record_pending_text || app->live_recording || app->live_record_full ||
                   app->live_record_limit_reached) {
                    const bool keyboard_opened = stop_and_open_recording_keyboard(app, true);
                    if(!keyboard_opened && !app->live_recording && !app->live_record_pending_text) {
                        exit_live_controller(app);
                    }
                } else {
                    exit_live_controller(app);
                }
            }
        } else if(key == InputKeyBack) {
            app->live_ui_screen = app->live_controller_return_screen;
        }
    } else if(app->state == RunError || key == InputKeyBack) {
        const bool returning_to_backup = app->backup_loaded;
        app->state = RunReady;
        app->ui_message[0] = '\0';
        if(returning_to_backup) {
            app->macro_ui_screen = MacroUiBackupDetails;
            app->backup_preview_playing = false;
        } else {
            app->macro_ui_screen = MacroUiList;
            refresh_macro_files(app);
            load_selected_macro(app);
        }
        if(app->worker) {
            furi_thread_join(app->worker);
            furi_thread_free(app->worker);
            app->worker = NULL;
        }
    }
}

static int32_t macro_worker(void* ctx) {
    SwitchMacroApp* app = ctx;
    app->state = RunSending;
    app->failed_endpoint = 0;
    app->tx_failure = TxFailureNone;
    app->usb_result = 0;

    bool failed = false;
    const bool preview_only = app->dev_fake_usb;
    do {
        app->completed_steps = 0U;
        app->macro_elapsed_ms = 0U;
        for(uint16_t i = 0; i < app->step_count && !app->stop_requested; i++) {
            const MacroStep* step = &app->steps[i];
            if(!preview_only && !send_gamepad_report(
                   app, step->buttons, step->hat, step->lx, step->ly, step->rx, step->ry)) {
                failed = true;
                break;
            }
            app->playback_step = i;
            view_port_update(app->view_port);
            if(!delay_or_cancel(app, step->duration_ms)) break;
            if(!preview_only && !step->state_mode &&
               !send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80)) {
                failed = true;
                break;
            }
            app->completed_steps = i + 1U;
            view_port_update(app->view_port);
        }
        if(failed || app->stop_requested) break;
        app->macro_cycles++;
    } while(app->macro_loop || app->macro_cycles < app->macro_repeat_count);

    if(app->connected && app->usb_dev) {
        const bool pad_queued = send_gamepad_report(app, 0, SWITCH_HAT_NONE, 0x80, 0x80, 0x80, 0x80);
        const bool pad_released = pad_queued && wait_report_complete(app, EP_GAMEPAD_IN);
        KeyboardReport keyboard = {0};
        const bool keyboard_sent = send_report(app, EP_KEYBOARD_IN, &keyboard, sizeof(keyboard));
        const bool keyboard_released = keyboard_sent && wait_report_complete(app, EP_KEYBOARD_IN);
        if(!pad_released || !keyboard_released) failed = true;
    }

    if(app->tx_failure != TxFailureNone || failed) {
        app->state = RunError;
        play_error_sound(app);
    } else if(app->stop_requested) {
        app->state = RunCanceled;
    } else {
        app->state = RunFinished;
    }
    view_port_update(app->view_port);
    return 0;
}

int32_t switch_macro_app(void* p) {
    UNUSED(p);
    SwitchMacroApp app = {0};
    active_app = &app;
    app.state = RunReady;
    app.macro_ui_screen = MacroUiHome;
    load_app_settings(&app);
    app.notification = furi_record_open(RECORD_NOTIFICATION);
    set_screen_awake(&app, app.settings.keep_screen_on);
    app.macro_repeat_count = 1U;
    app.live_record_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.previous_usb = furi_hal_usb_get_config();
    refresh_macro_files(&app);
    app.input_queue = furi_message_queue_alloc(UI_INPUT_QUEUE_DEPTH, sizeof(InputEvent));
    app.view_port = view_port_alloc();
    app.gui = furi_record_open(RECORD_GUI);
    view_port_draw_callback_set(app.view_port, app_draw, &app);
    view_port_input_callback_set(app.view_port, app_input, &app);
    gui_add_view_port(app.gui, app.view_port, GuiLayerFullscreen);
    furi_hal_usb_set_state_callback(usb_state_callback, &app);
    furi_check(furi_hal_usb_set_config(&switch_macro_usb, NULL));

    bool running = true;
    while(running) {
        service_controller_wake(&app);
        if(app.usb_flash_requested &&
           furi_get_tick() - app.usb_flash_started_tick >= furi_ms_to_ticks(ERROR_BLINK_DURATION_MS)) {
            app.usb_flash_requested = false;
            InputEvent pending_event;
            while(furi_message_queue_get(app.input_queue, &pending_event, 0U) == FuriStatusOk) {
                track_flash_input(&app, &pending_event);
            }
        }
        if(app.error_log_pending) {
            const TxFailure failure = app.error_log_pending_failure;
            app.error_log_pending = false;
            write_error_log(&app, tx_failure_text(failure));
        }
        advance_backup_preview(&app);
        advance_info_mascot(&app);
        if(app.state == RunLive && app.live_record_text &&
           (app.live_record_full || app.live_record_limit_reached)) {
            (void)stop_and_open_recording_keyboard(&app, false);
        }
        InputEvent event;
        if(furi_message_queue_get(app.input_queue, &event, 100) == FuriStatusOk) {
            const uint8_t key_mask = flash_key_mask(event.key);
            if(app.usb_flash_requested) {
                track_flash_input(&app, &event);
            } else if(key_mask != 0U && (app.flash_held_keys & key_mask)) {
                if(event.type == InputTypeRelease) track_flash_input(&app, &event);
            } else if(event.type == InputTypePress) {
                handle_input(&app, event.key, &running);
            } else if(app.state == RunReady && app.macro_ui_screen == MacroUiSimulator) {
                if(event.type == InputTypeRelease) {
                    release_simulator_button(&app, event.key);
                } else if(event.type == InputTypeRepeat && app.simulator_stick_active &&
                          simulator_selection_is_stick(app.simulator_selection)) {
                    handle_simulator_input(&app, event.key, true);
                }
            } else if(app.state == RunReady && app.macro_ui_screen == MacroUiCalibration &&
                      event.type == InputTypeRepeat) {
                handle_stick_calibration(&app, event.key);
            } else if(app.state == RunReady && event.type == InputTypeRepeat &&
                      app.macro_ui_screen != MacroUiSimulator) {
                handle_macro_ui_input(&app, event.key, &running);
            }
        }
        update_ui_error_sound(&app);
        view_port_update(app.view_port);
    }

    if(app.worker) {
        app.stop_requested = true;
        furi_thread_join(app.worker);
        furi_thread_free(app.worker);
    }
    stop_live_mode(&app);
    FURI_LOG_I(TAG, "App shutdown: Live mode stopped");
    finish_live_recording(&app);
    if(app.live_record_pending_text) (void)save_pending_live_recording(&app, NULL);
    (void)save_app_settings(&app);

    // Detach drawing/input first, but keep the view allocated until USB has
    // switched interfaces and can no longer dispatch configuration callbacks.
    gui_remove_view_port(app.gui, app.view_port);
    FURI_LOG_I(TAG, "App shutdown: view detached");
    if(app.steps) free(app.steps);

    // Stop USB callbacks before freeing their UI/transfer state. The USB host
    // can still issue configuration and endpoint events while this interface
    // is being replaced during app shutdown.
    furi_hal_usb_set_state_callback(NULL, NULL);
    active_app = NULL;
    const bool usb_restored = furi_hal_usb_set_config(app.previous_usb, NULL);
    if(!usb_restored) {
        FURI_LOG_E(TAG, "Failed to restore previous USB interface; disabling USB as fallback");
        // Leave the custom interface callbacks harmless if the HAL refuses
        // the preferred interface during host teardown.
        (void)furi_hal_usb_set_config(NULL, NULL);
    } else {
        FURI_LOG_I(TAG, "Previous USB interface restored");
    }
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);
    set_screen_awake(&app, false);
    if(app.notification) furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app.input_queue);
    if(app.gamepad_tx_sem) furi_semaphore_free(app.gamepad_tx_sem);
    if(app.keyboard_tx_sem) furi_semaphore_free(app.keyboard_tx_sem);
    if(app.live_record_mutex) furi_mutex_free(app.live_record_mutex);
    return 0;
}
