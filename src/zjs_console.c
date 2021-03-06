// Copyright (c) 2016-2017, Intel Corporation.

#ifdef BUILD_MODULE_CONSOLE

#include "zjs_common.h"
#include "zjs_error.h"
#include "zjs_util.h"
#ifdef ZJS_LINUX_BUILD
#include "zjs_linux_port.h"
#else
#include "zjs_zephyr_port.h"
#endif

#define MAX_STR_LENGTH   256

#ifdef ZJS_LINUX_BUILD
#define STDERR_PRINT(s, ...) fprintf(stderr, s, __VA_ARGS__)
#define STDOUT_PRINT(s, ...) fprintf(stdout, s, __VA_ARGS__)
#else
#define STDERR_PRINT(s, ...) ZJS_PRINT(s, __VA_ARGS__)
#define STDOUT_PRINT(s, ...) ZJS_PRINT(s, __VA_ARGS__)
#endif

#define IS_NUMBER 0
#define IS_INT    1
#define IS_UINT   2

static jerry_value_t gbl_time_obj;

static int is_int(jerry_value_t val) {
    int ret = 0;
    double n = jerry_get_number_value(val);
    ret = (n - (int) n == 0);
    if (ret) {
        // Integer type
        if (n < 0) {
            return IS_INT;
        } else {
            return IS_UINT;
        }
    } else {
        return IS_NUMBER;
    }
}

static bool value2str(const jerry_value_t value, char *buf, int maxlen,
                      bool quotes)
{
    // requires: buf has at least maxlen characters
    //  effects: writes a string representation of the value to buf; when
    //             processing a string value, puts quotes around it if quotes
    //             is true
    //  returns: true if the representation was complete or false if it
    //             was abbreviated
    if (jerry_value_is_array(value)) {
        uint32_t len = jerry_get_array_length(value);
        sprintf(buf, "[Array - length %lu]", len);
        return false;
    }
    else if (jerry_value_is_boolean(value)) {
        uint8_t val = jerry_get_boolean_value(value);
        sprintf(buf, (val) ? "true" : "false");
    }
    else if (jerry_value_is_function(value)) {
        sprintf(buf, "[Function]");
    }
    else if (jerry_value_is_number(value)) {
        int type = is_int(value);
        if (type == IS_NUMBER) {
#ifdef ZJS_PRINT_FLOATS
            double num = jerry_get_number_value(value);
            sprintf(buf, "%f", num);
#else
            int32_t num = (int32_t)jerry_get_number_value(value);
            sprintf(buf, "[Float ~%li]", num);
#endif
        } else if (type == IS_UINT) {
            uint32_t num = (uint32_t)jerry_get_number_value(value);
            sprintf(buf, "%lu", num);
        } else if (type == IS_INT) {
            int32_t num = (int32_t)jerry_get_number_value(value);
            // Linux and Zephyr print int32_t's differently if %li is used
#ifdef ZJS_LINUX_BUILD
            sprintf(buf, "%i", num);
#else
            sprintf(buf, "%li", num);
#endif
        }
    }
    else if (jerry_value_is_null(value)) {
        sprintf(buf, "null");
    }
    // NOTE: important that checks for function and array were above this
    else if (jerry_value_is_object(value)) {
        sprintf(buf, "[Object]");
    }
    else if (jerry_value_is_string(value)) {
        jerry_size_t size = jerry_get_string_size(value);
        if (size >= maxlen) {
            sprintf(buf, "[String - length %lu]", size);
        }
        else {
            char buffer[++size];
            zjs_copy_jstring(value, buffer, &size);
            if (quotes) {
                sprintf(buf, "\"%s\"", buffer);
            }
            else {
                sprintf(buf, "%s", buffer);
            }
        }
    }
    else if (jerry_value_is_undefined(value)) {
        sprintf(buf, "undefined");
    }
    else {
        // should never get this
        sprintf(buf, "UNKNOWN");
    }
    return true;
}

static void print_value(const jerry_value_t value, FILE *out, bool deep,
                        bool quotes)
{
    char buf[MAX_STR_LENGTH];
    if (!value2str(value, buf, MAX_STR_LENGTH, quotes) && deep) {
        if (jerry_value_is_array(value)) {
            uint32_t len = jerry_get_array_length(value);
            fprintf(out, "[");
            for (int i = 0; i < len; i++) {
                if (i) {
                    fprintf(out, ", ");
                }
                jerry_value_t element = jerry_get_property_by_index(value, i);
                print_value(element, out, false, true);
                jerry_release_value(element);
            }
            fprintf(out, "]");
        }
    }
    else {
        fprintf(out, "%s", buf);
    }
}

static jerry_value_t do_print(const jerry_value_t function_obj,
                              const jerry_value_t this,
                              const jerry_value_t argv[],
                              const jerry_length_t argc,
                              FILE* out)
{
    for (int i = 0; i < argc; i++) {
        if (i) {
            // insert spaces between arguments
            fprintf(out, " ");
        }
        print_value(argv[i], out, true, false);
    }
    fprintf(out, "\n");
    return ZJS_UNDEFINED;
}

static jerry_value_t console_log(const jerry_value_t function_obj,
                                 const jerry_value_t this,
                                 const jerry_value_t argv[],
                                 const jerry_length_t argc)
{
    return do_print(function_obj, this, argv, argc, stdout);
}

static jerry_value_t console_error(const jerry_value_t function_obj,
                                   const jerry_value_t this,
                                   const jerry_value_t argv[],
                                   const jerry_length_t argc)
{
    return do_print(function_obj, this, argv, argc, stderr);
}

static jerry_value_t console_time(const jerry_value_t function_obj,
                                  const jerry_value_t this,
                                  const jerry_value_t argv[],
                                  const jerry_length_t argc)
{
    if (argc < 1 || !jerry_value_is_string(argv[0])) {
        return TYPE_ERROR("invalid argument");
    }

    uint32_t start = zjs_port_timer_get_uptime();

    jerry_value_t num = jerry_create_number(start);
    jerry_set_property(gbl_time_obj, argv[0], num);
    jerry_release_value(num);
    return ZJS_UNDEFINED;
}

static jerry_value_t console_time_end(const jerry_value_t function_obj,
                                      const jerry_value_t this,
                                      const jerry_value_t argv[],
                                      const jerry_length_t argc)
{
    if (argc < 1 || !jerry_value_is_string(argv[0])) {
        return TYPE_ERROR("invalid argument");
    }

    jerry_value_t num = jerry_get_property(gbl_time_obj, argv[0]);
    jerry_delete_property(gbl_time_obj, argv[0]);

    if (!jerry_value_is_number(num)) {
        jerry_release_value(num);
        return TYPE_ERROR("unexpected value");
    }

    uint32_t start = (uint32_t)jerry_get_number_value(num);
    uint32_t milli = zjs_port_timer_get_uptime() - start;
    jerry_release_value(num);

    char *label = zjs_alloc_from_jstring(argv[0], NULL);
    const char *const_label = "unknown";
    if (label) {
        const_label = label;
    }

    ZJS_PRINT("%s: %lums\n", const_label, milli);
    zjs_free(label);
    return ZJS_UNDEFINED;
}

static jerry_value_t console_assert(const jerry_value_t function_obj,
                                    const jerry_value_t this,
                                    const jerry_value_t argv[],
                                    const jerry_length_t argc)
{
    char message[MAX_STR_LENGTH];
    if (!jerry_value_is_boolean(argv[0])) {
        return TYPE_ERROR("invalid parameter");
    }
    bool b = jerry_get_boolean_value(argv[0]);
    if (!b) {
        if (argc > 1) {
            value2str(argv[1], message, MAX_STR_LENGTH, false);
            return zjs_custom_error("AssertionError", message);
        } else {
            return zjs_custom_error("AssertionError", "console.assert");
        }
    }
    return ZJS_UNDEFINED;
}

void zjs_console_init(void)
{
    jerry_value_t console = jerry_create_object();
    zjs_obj_add_function(console, console_log, "log");
    zjs_obj_add_function(console, console_log, "info");
    zjs_obj_add_function(console, console_error, "error");
    zjs_obj_add_function(console, console_error, "warn");
    zjs_obj_add_function(console, console_time, "time");
    zjs_obj_add_function(console, console_time_end, "timeEnd");
    zjs_obj_add_function(console, console_assert, "assert");

    jerry_value_t global_obj = jerry_get_global_object();
    zjs_set_property(global_obj, "console", console);

    jerry_release_value(console);
    jerry_release_value(global_obj);

    // initialize the time object
    gbl_time_obj = jerry_create_object();
}

void zjs_console_cleanup()
{
    jerry_release_value(gbl_time_obj);
}

#endif  // BUILD_MODULE_CONSOLE
