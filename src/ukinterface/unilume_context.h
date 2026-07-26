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
    UL_STATUS_INTERNAL_ERROR = 4
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
void ul_engine_reset(UlEngineContext *context);

#ifdef __cplusplus
}
#endif

#endif
