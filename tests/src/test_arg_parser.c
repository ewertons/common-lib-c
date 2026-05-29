#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include <cmocka.h>

#include "arg_parser.h"
#include "tests.h"

/* ----------------------------------------------------------------------- */
/* Test handler helpers                                                     */
/* ----------------------------------------------------------------------- */

typedef struct test_handler_ctx
{
    bool     called;
    uint32_t parsed_count;
    arg_parser_parsed_arg_t args[16];
} test_handler_ctx_t;

static test_handler_ctx_t g_ctx;

static void reset_ctx(void)
{
    memset(&g_ctx, 0, sizeof(g_ctx));
}

static result_t capture_handler(arg_parser_parsed_t* parsed, const void* context)
{
    (void)context;
    g_ctx.called = true;
    g_ctx.parsed_count = parsed->arguments_count;
    for (uint32_t i = 0; i < parsed->arguments_count && i < 16; i++)
    {
        g_ctx.args[i] = parsed->arguments[i];
    }
    return ok;
}

static result_t failing_handler(arg_parser_parsed_t* parsed, const void* context)
{
    (void)parsed; (void)context;
    g_ctx.called = true;
    return error;
}

/* ----------------------------------------------------------------------- */
/* Test: basic command dispatch                                             */
/* ----------------------------------------------------------------------- */

static void test_basic_command_dispatch(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_command_t commands[] = {
        {
            .description = "A simple command",
            .name = "hello",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test app",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = NULL,
        .short_arguments_prefix = NULL,
    };

    char* argv[] = { "prog", "hello" };
    result_t r = arg_parser_process_args(&root, 2, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 0);
}

/* ----------------------------------------------------------------------- */
/* Test: command short name                                                */
/* ----------------------------------------------------------------------- */

static void test_command_short_name(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_command_t commands[] = {
        {
            .description = "List items",
            .name = "list",
            .short_name = "ls",
            .commands = NULL,
            .commands_count = 0,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test app",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = NULL,
        .short_arguments_prefix = NULL,
    };

    char* argv[] = { "prog", "ls" };
    result_t r = arg_parser_process_args(&root, 2, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: nested subcommands                                                */
/* ----------------------------------------------------------------------- */

static void test_nested_subcommands(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_command_t leaf_cmds[] = {
        {
            .description = "Send a request",
            .name = "request",
            .short_name = "req",
            .commands = NULL,
            .commands_count = 0,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = capture_handler,
        }
    };

    const arg_parser_command_t top_cmds[] = {
        {
            .description = "Pairing commands",
            .name = "pair",
            .short_name = NULL,
            .commands = leaf_cmds,
            .commands_count = 1,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = NULL,
        }
    };

    const arg_parser_t root = {
        .description = "Test app",
        .commands = top_cmds,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = NULL,
        .short_arguments_prefix = NULL,
    };

    char* argv[] = { "prog", "pair", "request" };
    result_t r = arg_parser_process_args(&root, 3, argv);
    assert_int_equal(r, ok);
    assert_true(g_ctx.called);

    /* Also test short name for subcommand. */
    reset_ctx();
    char* argv2[] = { "prog", "pair", "req" };
    r = arg_parser_process_args(&root, 3, argv2);
    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: named arguments with long prefix                                  */
/* ----------------------------------------------------------------------- */

static void test_named_arg_long(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 10,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Timeout in seconds",
            .name = "timeout",
            .short_name = "t",
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        },
        {
            .id = 20,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Verbose output",
            .name = "verbose",
            .short_name = "v",
            .value_type = ARG_PARSER_VALUE_TYPE_NONE,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Run something",
            .name = "run",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 2,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test app",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "run", "--timeout", "42", "--verbose" };
    result_t r = arg_parser_process_args(&root, 5, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 2);

    /* Find timeout arg. */
    bool found_timeout = false, found_verbose = false;
    for (uint32_t i = 0; i < g_ctx.parsed_count; i++)
    {
        if (g_ctx.args[i].id == 10)
        {
            assert_int_equal(g_ctx.args[i].value.int32, 42);
            assert_false(g_ctx.args[i].is_default);
            found_timeout = true;
        }
        if (g_ctx.args[i].id == 20)
        {
            assert_int_equal(g_ctx.args[i].value.int32, 1);
            assert_false(g_ctx.args[i].is_default);
            found_verbose = true;
        }
    }
    assert_true(found_timeout);
    assert_true(found_verbose);
}

/* ----------------------------------------------------------------------- */
/* Test: named arguments with short prefix                                 */
/* ----------------------------------------------------------------------- */

static void test_named_arg_short(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 5,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Port number",
            .name = "port",
            .short_name = "p",
            .value_type = ARG_PARSER_VALUE_TYPE_UINT32,
            .default_value = { .enabled = true, .value.uint32 = 8080 },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Start server",
            .name = "serve",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "serve", "-p", "9090" };
    result_t r = arg_parser_process_args(&root, 4, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 1);
    assert_int_equal(g_ctx.args[0].id, 5);
    assert_int_equal(g_ctx.args[0].value.uint32, 9090);
    assert_false(g_ctx.args[0].is_default);
}

/* ----------------------------------------------------------------------- */
/* Test: positional arguments                                              */
/* ----------------------------------------------------------------------- */

static void test_positional_args(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_POSITIONAL,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Source file",
            .name = "src",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        },
        {
            .id = 1,
            .type = ARG_PARSER_ARG_TYPE_POSITIONAL,
            .required = true,
            .multiple_allowed = false,
            .position = 1,
            .description = "Destination file",
            .name = "dst",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Copy a file",
            .name = "cp",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 2,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = NULL,
        .short_arguments_prefix = NULL,
    };

    char* argv[] = { "prog", "cp", "file_a.txt", "file_b.txt" };
    result_t r = arg_parser_process_args(&root, 4, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 2);

    /* Check by id. */
    bool found0 = false, found1 = false;
    for (uint32_t i = 0; i < g_ctx.parsed_count; i++)
    {
        if (g_ctx.args[i].id == 0)
        {
            assert_string_equal(g_ctx.args[i].value.string, "file_a.txt");
            found0 = true;
        }
        if (g_ctx.args[i].id == 1)
        {
            assert_string_equal(g_ctx.args[i].value.string, "file_b.txt");
            found1 = true;
        }
    }
    assert_true(found0);
    assert_true(found1);
}

/* ----------------------------------------------------------------------- */
/* Test: mixed named + positional arguments                                */
/* ----------------------------------------------------------------------- */

static void test_mixed_named_and_positional(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Timeout",
            .name = "timeout",
            .short_name = "t",
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = true, .value.int32 = 30 },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        },
        {
            .id = 1,
            .type = ARG_PARSER_ARG_TYPE_POSITIONAL,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Peer id",
            .name = "peer_id",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t leaf_cmds[] = {
        {
            .description = "Send pairing request",
            .name = "request",
            .short_name = "req",
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 2,
            .handler = capture_handler,
        }
    };

    const arg_parser_command_t top_cmds[] = {
        {
            .description = "Pairing",
            .name = "pair",
            .short_name = NULL,
            .commands = leaf_cmds,
            .commands_count = 1,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = NULL,
        }
    };

    const arg_parser_t root = {
        .description = "GoGoShare CLI",
        .commands = top_cmds,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    /* Named arg before positional: "pair request --timeout 10 peer123" */
    char* argv[] = { "ggs", "pair", "request", "--timeout", "10", "peer123" };
    result_t r = arg_parser_process_args(&root, 6, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 2);

    bool found_timeout = false, found_peer = false;
    for (uint32_t i = 0; i < g_ctx.parsed_count; i++)
    {
        if (g_ctx.args[i].id == 0)
        {
            assert_int_equal(g_ctx.args[i].value.int32, 10);
            assert_false(g_ctx.args[i].is_default);
            found_timeout = true;
        }
        if (g_ctx.args[i].id == 1)
        {
            assert_string_equal(g_ctx.args[i].value.string, "peer123");
            assert_false(g_ctx.args[i].is_default);
            found_peer = true;
        }
    }
    assert_true(found_timeout);
    assert_true(found_peer);
}

/* ----------------------------------------------------------------------- */
/* Test: default values applied when argument not provided                 */
/* ----------------------------------------------------------------------- */

static void test_default_values(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 7,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Retry count",
            .name = "retries",
            .short_name = "r",
            .value_type = ARG_PARSER_VALUE_TYPE_UINT32,
            .default_value = { .enabled = true, .value.uint32 = 3 },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        },
        {
            .id = 8,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Output format",
            .name = "format",
            .short_name = "f",
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = true, .value.string = "json" },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Fetch data",
            .name = "fetch",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 2,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    /* Provide neither argument — both should get defaults. */
    char* argv[] = { "prog", "fetch" };
    result_t r = arg_parser_process_args(&root, 2, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 2);

    bool found_retries = false, found_format = false;
    for (uint32_t i = 0; i < g_ctx.parsed_count; i++)
    {
        if (g_ctx.args[i].id == 7)
        {
            assert_int_equal(g_ctx.args[i].value.uint32, 3);
            assert_true(g_ctx.args[i].is_default);
            found_retries = true;
        }
        if (g_ctx.args[i].id == 8)
        {
            assert_string_equal(g_ctx.args[i].value.string, "json");
            assert_true(g_ctx.args[i].is_default);
            found_format = true;
        }
    }
    assert_true(found_retries);
    assert_true(found_format);
}

/* ----------------------------------------------------------------------- */
/* Test: required argument missing => error                                */
/* ----------------------------------------------------------------------- */

static void test_required_arg_missing(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Host",
            .name = "host",
            .short_name = "h",
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Connect",
            .name = "connect",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "connect" };
    result_t r = arg_parser_process_args(&root, 2, argv);

    assert_int_equal(r, invalid_argument);
    assert_false(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: unknown argument => error                                         */
/* ----------------------------------------------------------------------- */

static void test_unknown_argument(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_command_t commands[] = {
        {
            .description = "Do stuff",
            .name = "stuff",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "stuff", "--bogus", "val" };
    result_t r = arg_parser_process_args(&root, 4, argv);

    assert_int_equal(r, invalid_argument);
}

/* ----------------------------------------------------------------------- */
/* Test: value type validation (invalid int32)                             */
/* ----------------------------------------------------------------------- */

static void test_invalid_value_type(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Count",
            .name = "count",
            .short_name = "c",
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Test cmd",
            .name = "test",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "test", "--count", "not_a_number" };
    result_t r = arg_parser_process_args(&root, 4, argv);

    assert_int_equal(r, invalid_argument);
    assert_false(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: range restriction enforcement                                     */
/* ----------------------------------------------------------------------- */

static void test_range_restriction(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Volume",
            .name = "volume",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = false },
            .value_restrictions = {
                .enabled = true,
                .range = { .enabled = true, .min = 0, .max = 100, .step = 1 },
                .allowed = { .values = { .int32 = NULL }, .count = 0 },
            },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Set volume",
            .name = "vol",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    /* Within range — should succeed. */
    char* argv_ok[] = { "prog", "vol", "--volume", "50" };
    result_t r = arg_parser_process_args(&root, 4, argv_ok);
    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.args[0].value.int32, 50);

    /* Out of range — should fail. */
    reset_ctx();
    char* argv_bad[] = { "prog", "vol", "--volume", "150" };
    r = arg_parser_process_args(&root, 4, argv_bad);
    assert_int_equal(r, invalid_argument);
    assert_false(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: allowed values restriction                                        */
/* ----------------------------------------------------------------------- */

static void test_allowed_values_restriction(void** state)
{
    (void)state;
    reset_ctx();

    static const char* allowed_formats[] = { "json", "csv", "xml" };

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Output format",
            .name = "format",
            .short_name = "f",
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = false },
            .value_restrictions = {
                .enabled = true,
                .range = { .enabled = false, .min = 0, .max = 0, .step = 0 },
                .allowed = { .values.string = allowed_formats, .count = 3 },
            },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Export",
            .name = "export",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    /* Valid value. */
    char* argv_ok[] = { "prog", "export", "--format", "csv" };
    result_t r = arg_parser_process_args(&root, 4, argv_ok);
    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_string_equal(g_ctx.args[0].value.string, "csv");

    /* Invalid value. */
    reset_ctx();
    char* argv_bad[] = { "prog", "export", "--format", "yaml" };
    r = arg_parser_process_args(&root, 4, argv_bad);
    assert_int_equal(r, invalid_argument);
    assert_false(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: custom validator                                                   */
/* ----------------------------------------------------------------------- */

static result_t validate_even(arg_parser_value_t value, arg_parser_value_type_t type)
{
    (void)type;
    if (value.int32 % 2 != 0) return invalid_argument;
    return ok;
}

static void test_custom_validator(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Even number",
            .name = "num",
            .short_name = "n",
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = validate_even,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Test",
            .name = "check",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    /* Even number — ok. */
    char* argv_ok[] = { "prog", "check", "--num", "8" };
    result_t r = arg_parser_process_args(&root, 4, argv_ok);
    assert_int_equal(r, ok);
    assert_true(g_ctx.called);

    /* Odd number — fail. */
    reset_ctx();
    char* argv_bad[] = { "prog", "check", "--num", "7" };
    r = arg_parser_process_args(&root, 4, argv_bad);
    assert_int_equal(r, invalid_argument);
    assert_false(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: handler return value is propagated                                */
/* ----------------------------------------------------------------------- */

static void test_handler_return_propagated(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_command_t commands[] = {
        {
            .description = "Fail",
            .name = "fail",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = failing_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = NULL,
        .short_arguments_prefix = NULL,
    };

    char* argv[] = { "prog", "fail" };
    result_t r = arg_parser_process_args(&root, 2, argv);

    assert_int_equal(r, error);
    assert_true(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: get_argument_by_id returns correct entry or NULL                   */
/* ----------------------------------------------------------------------- */

static result_t check_get_by_id_handler(arg_parser_parsed_t* parsed, const void* context)
{
    (void)context;
    g_ctx.called = true;

    arg_parser_parsed_arg_t* found = arg_parser_get_argument_by_id(parsed, 42);
    assert_non_null(found);
    assert_int_equal(found->value.int32, -5);

    arg_parser_parsed_arg_t* missing = arg_parser_get_argument_by_id(parsed, 999);
    assert_null(missing);

    return ok;
}

static void test_get_argument_by_id(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 42,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Val",
            .name = "val",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Test",
            .name = "cmd",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 1,
            .handler = check_get_by_id_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "cmd", "--val", "-5" };
    result_t r = arg_parser_process_args(&root, 4, argv);
    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: double and char value types                                        */
/* ----------------------------------------------------------------------- */

static void test_double_and_char_types(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_arg_t args_def[] = {
        {
            .id = 0,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Ratio",
            .name = "ratio",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_DOUBLE,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        },
        {
            .id = 1,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Delimiter",
            .name = "delim",
            .short_name = "d",
            .value_type = ARG_PARSER_VALUE_TYPE_CHAR,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t commands[] = {
        {
            .description = "Process",
            .name = "proc",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = args_def,
            .arguments_count = 2,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    char* argv[] = { "prog", "proc", "--ratio", "3.14", "-d", "," };
    result_t r = arg_parser_process_args(&root, 6, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 2);

    bool found_ratio = false, found_delim = false;
    for (uint32_t i = 0; i < g_ctx.parsed_count; i++)
    {
        if (g_ctx.args[i].id == 0)
        {
            assert_true(g_ctx.args[i].value.dbl > 3.13 && g_ctx.args[i].value.dbl < 3.15);
            found_ratio = true;
        }
        if (g_ctx.args[i].id == 1)
        {
            assert_int_equal(g_ctx.args[i].value.character, ',');
            found_delim = true;
        }
    }
    assert_true(found_ratio);
    assert_true(found_delim);
}

/* ----------------------------------------------------------------------- */
/* Test: unknown command => error                                           */
/* ----------------------------------------------------------------------- */

static void test_unknown_command(void** state)
{
    (void)state;
    reset_ctx();

    const arg_parser_command_t commands[] = {
        {
            .description = "Known",
            .name = "known",
            .short_name = NULL,
            .commands = NULL,
            .commands_count = 0,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = capture_handler,
        }
    };

    const arg_parser_t root = {
        .description = "Test",
        .commands = commands,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = NULL,
        .short_arguments_prefix = NULL,
    };

    char* argv[] = { "prog", "bogus" };
    result_t r = arg_parser_process_args(&root, 2, argv);

    assert_int_equal(r, invalid_argument);
    assert_false(g_ctx.called);
}

/* ----------------------------------------------------------------------- */
/* Test: complex scenario — multi-level commands with all features          */
/* ----------------------------------------------------------------------- */

static result_t complex_handler(arg_parser_parsed_t* parsed, const void* context)
{
    (void)context;
    g_ctx.called = true;
    g_ctx.parsed_count = parsed->arguments_count;

    /* Verify we can use the getter API inside the handler. */
    arg_parser_parsed_arg_t* host = arg_parser_get_argument_by_id(parsed, 100);
    assert_non_null(host);
    assert_string_equal(host->value.string, "10.0.0.1");

    arg_parser_parsed_arg_t* port = arg_parser_get_argument_by_id(parsed, 101);
    assert_non_null(port);
    assert_int_equal(port->value.uint32, 443);
    assert_true(port->is_default); /* Not provided, should be default. */

    arg_parser_parsed_arg_t* proto = arg_parser_get_argument_by_id(parsed, 102);
    assert_non_null(proto);
    assert_string_equal(proto->value.string, "tcp");

    arg_parser_parsed_arg_t* retries = arg_parser_get_argument_by_id(parsed, 103);
    assert_non_null(retries);
    assert_int_equal(retries->value.int32, 5);
    assert_false(retries->is_default);

    return ok;
}

static void test_complex_full_scenario(void** state)
{
    (void)state;
    reset_ctx();

    static const char* allowed_protos[] = { "tcp", "udp", "quic" };

    const arg_parser_arg_t deploy_args[] = {
        {
            .id = 100,
            .type = ARG_PARSER_ARG_TYPE_POSITIONAL,
            .required = true,
            .multiple_allowed = false,
            .position = 0,
            .description = "Target host",
            .name = "host",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = false },
            .value_restrictions = { .enabled = false },
            .custom_validator = NULL,
        },
        {
            .id = 101,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Port number",
            .name = "port",
            .short_name = "p",
            .value_type = ARG_PARSER_VALUE_TYPE_UINT32,
            .default_value = { .enabled = true, .value.uint32 = 443 },
            .value_restrictions = {
                .enabled = true,
                .range = { .enabled = true, .min = 1, .max = 65535, .step = 1 },
                .allowed = { .values = { .uint32 = NULL }, .count = 0 },
            },
            .custom_validator = NULL,
        },
        {
            .id = 102,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Protocol",
            .name = "protocol",
            .short_name = NULL,
            .value_type = ARG_PARSER_VALUE_TYPE_STRING,
            .default_value = { .enabled = true, .value.string = "tcp" },
            .value_restrictions = {
                .enabled = true,
                .range = { .enabled = false, .min = 0, .max = 0, .step = 0 },
                .allowed = { .values.string = allowed_protos, .count = 3 },
            },
            .custom_validator = NULL,
        },
        {
            .id = 103,
            .type = ARG_PARSER_ARG_TYPE_NAMED,
            .required = false,
            .multiple_allowed = false,
            .position = 0,
            .description = "Number of retries",
            .name = "retries",
            .short_name = "r",
            .value_type = ARG_PARSER_VALUE_TYPE_INT32,
            .default_value = { .enabled = true, .value.int32 = 3 },
            .value_restrictions = {
                .enabled = true,
                .range = { .enabled = true, .min = 0, .max = 10, .step = 1 },
                .allowed = { .values = { .int32 = NULL }, .count = 0 },
            },
            .custom_validator = NULL,
        }
    };

    const arg_parser_command_t service_cmds[] = {
        {
            .description = "Deploy a service instance",
            .name = "deploy",
            .short_name = "dep",
            .commands = NULL,
            .commands_count = 0,
            .arguments = deploy_args,
            .arguments_count = 4,
            .handler = complex_handler,
        }
    };

    const arg_parser_command_t top_cmds[] = {
        {
            .description = "Service management",
            .name = "service",
            .short_name = "svc",
            .commands = service_cmds,
            .commands_count = 1,
            .arguments = NULL,
            .arguments_count = 0,
            .handler = NULL,
        }
    };

    const arg_parser_t root = {
        .description = "Complex CLI tool",
        .commands = top_cmds,
        .commands_count = 1,
        .arguments = NULL,
        .arguments_count = 0,
        .arguments_prefix = "--",
        .short_arguments_prefix = "-",
    };

    /* "prog svc dep -r 5 10.0.0.1"
     * - Uses short command names
     * - Named arg "-r 5" overrides default
     * - Positional "10.0.0.1" for host
     * - --port and --protocol get defaults (443 and "tcp") */
    char* argv[] = { "prog", "svc", "dep", "-r", "5", "10.0.0.1" };
    result_t r = arg_parser_process_args(&root, 6, argv);

    assert_int_equal(r, ok);
    assert_true(g_ctx.called);
    assert_int_equal(g_ctx.parsed_count, 4);
}

/* ----------------------------------------------------------------------- */
/* Test: null root returns invalid_argument                                */
/* ----------------------------------------------------------------------- */

static void test_null_root(void** state)
{
    (void)state;
    char* argv[] = { "prog" };
    result_t r = arg_parser_process_args(NULL, 1, argv);
    assert_int_equal(r, invalid_argument);
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

int test_arg_parser(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_basic_command_dispatch),
        cmocka_unit_test(test_command_short_name),
        cmocka_unit_test(test_nested_subcommands),
        cmocka_unit_test(test_named_arg_long),
        cmocka_unit_test(test_named_arg_short),
        cmocka_unit_test(test_positional_args),
        cmocka_unit_test(test_mixed_named_and_positional),
        cmocka_unit_test(test_default_values),
        cmocka_unit_test(test_required_arg_missing),
        cmocka_unit_test(test_unknown_argument),
        cmocka_unit_test(test_invalid_value_type),
        cmocka_unit_test(test_range_restriction),
        cmocka_unit_test(test_allowed_values_restriction),
        cmocka_unit_test(test_custom_validator),
        cmocka_unit_test(test_handler_return_propagated),
        cmocka_unit_test(test_get_argument_by_id),
        cmocka_unit_test(test_double_and_char_types),
        cmocka_unit_test(test_unknown_command),
        cmocka_unit_test(test_complex_full_scenario),
        cmocka_unit_test(test_null_root),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
