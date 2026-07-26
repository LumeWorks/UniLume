// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef UNILUME_CONTEXT_H
#define UNILUME_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UlEngineContext UlEngineContext;

typedef enum UlStatus {
    UL_STATUS_OK = 0,
    UL_STATUS_INVALID_ARGUMENT = 1,
    UL_STATUS_OUT_OF_MEMORY = 2,
    UL_STATUS_OUTPUT_TOO_SMALL = 3,
    UL_STATUS_INTERNAL_ERROR = 4,
    UL_STATUS_INVALID_UTF8 = 5,
    UL_STATUS_DUPLICATE = 6,
    UL_STATUS_LIMIT_EXCEEDED = 7
} UlStatus;

typedef enum UlInputMethod {
    UL_INPUT_METHOD_TELEX = 0,
    UL_INPUT_METHOD_VNI = 1,
    UL_INPUT_METHOD_VIQR = 2
} UlInputMethod;

typedef struct UlEngineOptions {
    int spell_check;
    int free_marking;
    int modern_tone;
    int auto_restore;
} UlEngineOptions;

typedef struct UlEngineEdit {
    int handled;
    int32_t delete_before_cursor;
    size_t output_size;
} UlEngineEdit;

typedef struct UlMacroEntry {
    const char *key;
    size_t key_size;
    const char *text;
    size_t text_size;
} UlMacroEntry;

typedef enum UlMacroTrigger {
    UL_MACRO_TRIGGER_WORD_BOUNDARY = 0
} UlMacroTrigger;

typedef enum UlMacroCapitalization {
    UL_MACRO_CAPITALIZATION_EXACT = 0
} UlMacroCapitalization;

typedef struct UlMacroOptions {
    int enabled;
    UlMacroTrigger trigger;
    UlMacroCapitalization capitalization;
} UlMacroOptions;

typedef enum UlKeymapAction {
    UL_KEYMAP_TONE0 = 0,
    UL_KEYMAP_TONE1,
    UL_KEYMAP_TONE2,
    UL_KEYMAP_TONE3,
    UL_KEYMAP_TONE4,
    UL_KEYMAP_TONE5,
    UL_KEYMAP_ROOF_ALL,
    UL_KEYMAP_ROOF_A,
    UL_KEYMAP_ROOF_E,
    UL_KEYMAP_ROOF_O,
    UL_KEYMAP_HOOK_ALL,
    UL_KEYMAP_HOOK_UO,
    UL_KEYMAP_HOOK_U,
    UL_KEYMAP_HOOK_O,
    UL_KEYMAP_BOWL,
    UL_KEYMAP_D_MARK,
    UL_KEYMAP_TELEX_W,
    UL_KEYMAP_ESCAPE,
    UL_KEYMAP_ACTION_COUNT
} UlKeymapAction;

typedef struct UlKeymapEntry {
    uint32_t key;
    UlKeymapAction action;
} UlKeymapEntry;

UlStatus ul_engine_create(UlInputMethod method, UlEngineContext **out_context);
void ul_engine_destroy(UlEngineContext *context);

UlStatus ul_engine_process_ascii(UlEngineContext *context,
                                 uint32_t key,
                                 int shift_pressed,
                                 int caps_lock_on,
                                 char *output,
                                 size_t output_capacity,
                                 UlEngineEdit *edit);

UlStatus ul_engine_backspace(UlEngineContext *context,
                             char *output,
                             size_t output_capacity,
                             UlEngineEdit *edit);

UlStatus ul_engine_set_input_method(UlEngineContext *context,
                                    UlInputMethod method);
UlStatus ul_engine_get_options(const UlEngineContext *context,
                               UlEngineOptions *out_options);
UlStatus ul_engine_set_options(UlEngineContext *context,
                               const UlEngineOptions *options);
UlStatus ul_engine_set_macros(UlEngineContext *context,
                              const UlMacroEntry *entries,
                              size_t entry_count,
                              const UlMacroOptions *options);
UlStatus ul_engine_set_keymap(UlEngineContext *context,
                              const UlKeymapEntry *entries,
                              size_t entry_count);
void ul_engine_reset(UlEngineContext *context);

#ifdef __cplusplus
}
#endif

#endif
