// SPDX-License-Identifier: GPL-2.0-or-later

#include "unilume_context.h"

#include "ukengine.h"
#include "vnconv.h"

#include <climits>
#include <new>

struct UlEngineContext {
    UkSharedMem control;
    UkEngine engine;
};

namespace {

bool isSupportedMethod(UlInputMethod method)
{
    return method == UL_INPUT_METHOD_TELEX ||
           method == UL_INPUT_METHOD_VNI ||
           method == UL_INPUT_METHOD_VIQR;
}

UkInputMethod toLegacyMethod(UlInputMethod method)
{
    switch (method) {
    case UL_INPUT_METHOD_VNI:
        return UkVni;
    case UL_INPUT_METHOD_VIQR:
        return UkViqr;
    case UL_INPUT_METHOD_TELEX:
    default:
        return UkTelex;
    }
}

void setDefaultOptions(UnikeyOptions &options)
{
    options.freeMarking = 1;
    options.modernStyle = 0;
    options.macroEnabled = 0;
    options.useUnicodeClipboard = 0;
    options.alwaysMacro = 0;
    options.strictSpellCheck = 0;
    options.useIME = 0;
    options.spellCheckEnabled = 1;
    options.autoNonVnRestore = 1;
}

bool isBoolean(int value)
{
    return value == 0 || value == 1;
}

bool isValidOptions(const UlEngineOptions &options)
{
    return isBoolean(options.spell_check) &&
           isBoolean(options.free_marking) &&
           isBoolean(options.modern_tone) &&
           isBoolean(options.auto_restore);
}

UlEngineOptions toPublicOptions(const UnikeyOptions &options)
{
    return {options.spellCheckEnabled, options.freeMarking,
            options.modernStyle, options.autoNonVnRestore};
}

void applyOptions(UnikeyOptions &destination,
                  const UlEngineOptions &source)
{
    destination.spellCheckEnabled = source.spell_check;
    destination.freeMarking = source.free_marking;
    destination.modernStyle = source.modern_tone;
    destination.autoNonVnRestore = source.auto_restore;
}

UlStatus validateOutputArguments(char *output,
                                 size_t output_capacity,
                                 UlEngineEdit *edit)
{
    if (edit == 0 || output == 0 || output_capacity == 0 ||
        output_capacity > static_cast<size_t>(INT_MAX)) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    return UL_STATUS_OK;
}

void clearEdit(UlEngineEdit &edit)
{
    edit.handled = 0;
    edit.delete_before_cursor = 0;
    edit.output_size = 0;
}

} // namespace

UlStatus ul_engine_create(UlInputMethod method, UlEngineContext **out_context)
{
    if (out_context == 0 || !isSupportedMethod(method)) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    *out_context = 0;

    SetupUnikeyEngine();

    UlEngineContext *context = new (std::nothrow) UlEngineContext;
    if (context == 0) {
        return UL_STATUS_OUT_OF_MEMORY;
    }

    context->control.input.init();
    context->control.macStore.init();
    context->control.vietKey = 1;
    context->control.iconShown = 0;
    context->control.usrKeyMapLoaded = 0;
    context->control.charsetId = CONV_CHARSET_XUTF8;
    context->control.initialized = 1;
    setDefaultOptions(context->control.options);
    context->control.input.setIM(toLegacyMethod(method));

    context->engine.setCtrlInfo(&context->control);
    context->engine.setCheckKbCaseFunc(0);
    context->engine.setCapsState(0, 0);
    context->engine.reset();

    *out_context = context;
    return UL_STATUS_OK;
}

void ul_engine_destroy(UlEngineContext *context)
{
    delete context;
}

UlStatus ul_engine_process_ascii(UlEngineContext *context,
                                 uint32_t key,
                                 int shift_pressed,
                                 int caps_lock_on,
                                 char *output,
                                 size_t output_capacity,
                                 UlEngineEdit *edit)
{
    if (context == 0 || key > 0x7f) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    const UlStatus validation =
        validateOutputArguments(output, output_capacity, edit);
    if (validation != UL_STATUS_OK) {
        return validation;
    }

    clearEdit(*edit);
    int output_size = static_cast<int>(output_capacity);
    int backspaces = 0;
    UkOutputType output_type = UkCharOutput;
    context->engine.setCapsState(shift_pressed, caps_lock_on);
    const int handled = context->engine.process(
        key,
        backspaces,
        reinterpret_cast<unsigned char *>(output),
        output_size,
        output_type);

    if (output_size < 0 ||
        static_cast<size_t>(output_size) > output_capacity) {
        context->engine.reset();
        return UL_STATUS_OUTPUT_TOO_SMALL;
    }
    edit->handled = handled != 0;
    edit->delete_before_cursor = backspaces;
    edit->output_size = static_cast<size_t>(output_size);
    return UL_STATUS_OK;
}

UlStatus ul_engine_backspace(UlEngineContext *context,
                             char *output,
                             size_t output_capacity,
                             UlEngineEdit *edit)
{
    if (context == 0) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    const UlStatus validation =
        validateOutputArguments(output, output_capacity, edit);
    if (validation != UL_STATUS_OK) {
        return validation;
    }

    clearEdit(*edit);
    int output_size = static_cast<int>(output_capacity);
    int backspaces = 0;
    UkOutputType output_type = UkCharOutput;
    const int handled = context->engine.processBackspace(
        backspaces,
        reinterpret_cast<unsigned char *>(output),
        output_size,
        output_type);

    if (output_size < 0 ||
        static_cast<size_t>(output_size) > output_capacity) {
        context->engine.reset();
        return UL_STATUS_OUTPUT_TOO_SMALL;
    }
    edit->handled = handled != 0 || backspaces != 0 || output_size != 0;
    edit->delete_before_cursor = backspaces;
    edit->output_size = static_cast<size_t>(output_size);
    return UL_STATUS_OK;
}

UlStatus ul_engine_set_input_method(UlEngineContext *context,
                                    UlInputMethod method)
{
    if (context == 0 || !isSupportedMethod(method)) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    context->control.input.setIM(toLegacyMethod(method));
    context->engine.reset();
    return UL_STATUS_OK;
}

UlStatus ul_engine_get_options(const UlEngineContext *context,
                               UlEngineOptions *out_options)
{
    if (context == 0 || out_options == 0) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    *out_options = toPublicOptions(context->control.options);
    return UL_STATUS_OK;
}

UlStatus ul_engine_set_options(UlEngineContext *context,
                               const UlEngineOptions *options)
{
    if (context == 0 || options == 0 || !isValidOptions(*options)) {
        return UL_STATUS_INVALID_ARGUMENT;
    }
    applyOptions(context->control.options, *options);
    context->engine.reset();
    return UL_STATUS_OK;
}

void ul_engine_reset(UlEngineContext *context)
{
    if (context != 0) {
        context->engine.reset();
    }
}
