#include "arg_parser2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* ----------------------------------------------------------------------- */
/* Internal constants                                                      */
/* ----------------------------------------------------------------------- */

#define DEFAULT_PREFIX       "--"
#define DEFAULT_SHORT_PREFIX "-"
#define MAX_PARSED_ARGS      64

/* ----------------------------------------------------------------------- */
/* Helpers                                                                 */
/* ----------------------------------------------------------------------- */

static const char* get_prefix(const arg_parser2_t* root)
{
    return (root->arguments_prefix != NULL) ? root->arguments_prefix : DEFAULT_PREFIX;
}

static const char* get_short_prefix(const arg_parser2_t* root)
{
    return (root->short_arguments_prefix != NULL) ? root->short_arguments_prefix : DEFAULT_SHORT_PREFIX;
}

static bool str_starts_with(const char* s, const char* prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* ----------------------------------------------------------------------- */
/* Value parsing                                                           */
/* ----------------------------------------------------------------------- */

static result_t parse_value(const char* str, arg_parser2_value_type_t type, arg_parser2_value_t* out)
{
    if (str == NULL) return invalid_argument;

    switch (type)
    {
        case ARG_PARSER_VALUE_TYPE_INT32:
        {
            char* end = NULL;
            errno = 0;
            long v = strtol(str, &end, 10);
            if (end == str || *end != '\0' || errno == ERANGE || v < INT32_MIN || v > INT32_MAX)
            {
                return invalid_argument;
            }
            out->int32 = (int32_t)v;
            return ok;
        }
        case ARG_PARSER_VALUE_TYPE_UINT32:
        {
            char* end = NULL;
            errno = 0;
            unsigned long v = strtoul(str, &end, 10);
            if (end == str || *end != '\0' || errno == ERANGE || v > UINT32_MAX)
            {
                return invalid_argument;
            }
            out->uint32 = (uint32_t)v;
            return ok;
        }
        case ARG_PARSER_VALUE_TYPE_STRING:
        {
            out->string = str;
            return ok;
        }
        case ARG_PARSER_VALUE_TYPE_CHAR:
        {
            if (strlen(str) != 1)
            {
                return invalid_argument;
            }
            out->character = str[0];
            return ok;
        }
        case ARG_PARSER_VALUE_TYPE_DOUBLE:
        {
            char* end = NULL;
            errno = 0;
            double v = strtod(str, &end);
            if (end == str || *end != '\0' || errno == ERANGE)
            {
                return invalid_argument;
            }
            out->dbl = v;
            return ok;
        }
        case ARG_PARSER_VALUE_TYPE_NONE:
        default:
            return invalid_argument;
    }
}

/* ----------------------------------------------------------------------- */
/* Restriction validation                                                  */
/* ----------------------------------------------------------------------- */

static double value_to_double(arg_parser2_value_t val, arg_parser2_value_type_t type)
{
    switch (type)
    {
        case ARG_PARSER_VALUE_TYPE_INT32:  return (double)val.int32;
        case ARG_PARSER_VALUE_TYPE_UINT32: return (double)val.uint32;
        case ARG_PARSER_VALUE_TYPE_DOUBLE: return val.dbl;
        default: return 0.0;
    }
}

static result_t validate_restrictions(
    const arg_parser2_arg_t* arg_def, arg_parser2_value_t value)
{
    if (!arg_def->value_restrictions.enabled) return ok;

    const arg_parser2_value_restrictions_t* r = &arg_def->value_restrictions;

    /* Check allowed values first (takes precedence over range). */
    if (r->allowed.count > 0)
    {
        bool found = false;
        for (uint32_t i = 0; i < r->allowed.count; i++)
        {
            switch (arg_def->value_type)
            {
                case ARG_PARSER_VALUE_TYPE_STRING:
                    if (strcmp(value.string, r->allowed.values.string[i]) == 0)
                        found = true;
                    break;
                case ARG_PARSER_VALUE_TYPE_INT32:
                    if (value.int32 == r->allowed.values.int32[i])
                        found = true;
                    break;
                case ARG_PARSER_VALUE_TYPE_UINT32:
                    if (value.uint32 == r->allowed.values.uint32[i])
                        found = true;
                    break;
                case ARG_PARSER_VALUE_TYPE_DOUBLE:
                    if (value.dbl == r->allowed.values.dbl[i])
                        found = true;
                    break;
                default:
                    break;
            }
            if (found) break;
        }

        if (!found)
        {
            fprintf(stderr, "error: value for '%s' is not among allowed values\n",
                    arg_def->name ? arg_def->name : arg_def->short_name);
            return invalid_argument;
        }
        return ok;
    }

    /* Check range. */
    if (r->range.enabled)
    {
        double v = value_to_double(value, arg_def->value_type);
        if (v < r->range.min || v > r->range.max)
        {
            fprintf(stderr, "error: value for '%s' out of range [%.0f, %.0f]\n",
                    arg_def->name ? arg_def->name : arg_def->short_name,
                    r->range.min, r->range.max);
            return invalid_argument;
        }
    }

    return ok;
}

/* ----------------------------------------------------------------------- */
/* Help printing                                                           */
/* ----------------------------------------------------------------------- */

static void print_command_help(const arg_parser2_command_t* cmd, const char* path,
                               const char* long_pfx, const char* short_pfx)
{
    if (cmd->description) fprintf(stderr, "%s", cmd->description);
    fprintf(stderr, "\n");

    /* Usage line. */
    bool has_named = false;
    for (uint32_t i = 0; i < cmd->arguments_count; i++)
    {
        if (cmd->arguments[i].type == ARG_PARSER_ARG_TYPE_NAMED)
        {
            has_named = true;
            break;
        }
    }

    fprintf(stderr, "\nUsage:\n  %s", path);
    if (has_named) fprintf(stderr, " [options]");
    for (uint32_t i = 0; i < cmd->arguments_count; i++)
    {
        const arg_parser2_arg_t* a = &cmd->arguments[i];
        if (a->type == ARG_PARSER_ARG_TYPE_POSITIONAL)
        {
            if (a->required)
                fprintf(stderr, " <%s>", a->name ? a->name : "arg");
            else
                fprintf(stderr, " [%s]", a->name ? a->name : "arg");
        }
    }
    if (cmd->commands_count > 0) fprintf(stderr, " <subcommand>");
    fprintf(stderr, "\n");

    if (cmd->commands_count > 0)
    {
        fprintf(stderr, "\nSubcommands:\n");
        for (uint32_t i = 0; i < cmd->commands_count; i++)
        {
            fprintf(stderr, "  %-16s %s\n",
                    cmd->commands[i].name,
                    cmd->commands[i].description ? cmd->commands[i].description : "");
        }
    }

    if (cmd->arguments_count > 0)
    {
        fprintf(stderr, "\nArguments:\n");
        for (uint32_t i = 0; i < cmd->arguments_count; i++)
        {
            const arg_parser2_arg_t* a = &cmd->arguments[i];
            if (a->type == ARG_PARSER_ARG_TYPE_NAMED)
            {
                fprintf(stderr, "  ");
                if (a->short_name) fprintf(stderr, "%s%s", short_pfx, a->short_name);
                if (a->short_name && a->name) fprintf(stderr, ", ");
                if (a->name) fprintf(stderr, "%s%s", long_pfx, a->name);
                if (a->value_type != ARG_PARSER_VALUE_TYPE_NONE)
                    fprintf(stderr, " <value>");
                if (a->description) fprintf(stderr, "\t%s", a->description);
                if (a->required) fprintf(stderr, " (required)");
                fprintf(stderr, "\n");
            }
            else
            {
                fprintf(stderr, "  <%s>", a->name ? a->name : "arg");
                if (a->description) fprintf(stderr, "\t%s", a->description);
                if (a->required) fprintf(stderr, " (required)");
                fprintf(stderr, "\n");
            }
        }
    }

    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  %shelp             Show this help message\n", long_pfx);
    fprintf(stderr, "  %slong-help        Show detailed help for all subcommands\n", long_pfx);
}

static void print_root_help(const arg_parser2_t* root)
{
    const char* long_pfx = get_prefix(root);

    if (root->description) fprintf(stderr, "%s\n", root->description);
    fprintf(stderr, "\nCommands:\n");
    for (uint32_t i = 0; i < root->commands_count; i++)
    {
        fprintf(stderr, "  %-16s %s\n",
                root->commands[i].name,
                root->commands[i].description ? root->commands[i].description : "");
    }

    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  %shelp             Show help for a command\n", long_pfx);
    fprintf(stderr, "  %slong-help        Show detailed help for all commands\n", long_pfx);
}

/* Recursively print help for a command and all its subcommands (for --long-help). */
static void print_long_help_cmd(const arg_parser2_command_t* cmd, const char* long_pfx,
                                const char* short_pfx, int depth)
{
    /* Indent based on depth. */
    for (int d = 0; d < depth; d++) fprintf(stderr, "  ");
    fprintf(stderr, "%s", cmd->name);
    if (cmd->short_name) fprintf(stderr, " (%s)", cmd->short_name);
    if (cmd->description) fprintf(stderr, " - %s", cmd->description);
    fprintf(stderr, "\n");

    if (cmd->arguments_count > 0)
    {
        for (uint32_t i = 0; i < cmd->arguments_count; i++)
        {
            const arg_parser2_arg_t* a = &cmd->arguments[i];
            for (int d = 0; d < depth + 1; d++) fprintf(stderr, "  ");
            if (a->type == ARG_PARSER_ARG_TYPE_NAMED)
            {
                if (a->short_name) fprintf(stderr, "%s%s", short_pfx, a->short_name);
                if (a->short_name && a->name) fprintf(stderr, ", ");
                if (a->name) fprintf(stderr, "%s%s", long_pfx, a->name);
                if (a->value_type != ARG_PARSER_VALUE_TYPE_NONE)
                    fprintf(stderr, " <value>");
            }
            else
            {
                fprintf(stderr, "<%s>", a->name ? a->name : "arg");
            }
            if (a->description) fprintf(stderr, "  %s", a->description);
            if (a->required) fprintf(stderr, " (required)");
            fprintf(stderr, "\n");
        }
    }

    for (uint32_t i = 0; i < cmd->commands_count; i++)
    {
        print_long_help_cmd(&cmd->commands[i], long_pfx, short_pfx, depth + 1);
    }
}

static void print_long_help(const arg_parser2_t* root)
{
    const char* long_pfx  = get_prefix(root);
    const char* short_pfx = get_short_prefix(root);

    if (root->description) fprintf(stderr, "%s\n", root->description);
    fprintf(stderr, "\n");

    for (uint32_t i = 0; i < root->commands_count; i++)
    {
        if (i > 0) fprintf(stderr, "\n");
        print_long_help_cmd(&root->commands[i], long_pfx, short_pfx, 0);
    }

    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  %shelp             Show help for a command\n", long_pfx);
    fprintf(stderr, "  %slong-help        Show detailed help for all commands\n", long_pfx);
}

/* Check if a token is the --help flag. */
static bool is_help_flag(const char* token, const char* long_pfx)
{
    char help_flag[32];
    snprintf(help_flag, sizeof(help_flag), "%shelp", long_pfx);
    return strcmp(token, help_flag) == 0;
}

/* Check if a token is the --long-help flag. */
static bool is_long_help_flag(const char* token, const char* long_pfx)
{
    char flag[32];
    snprintf(flag, sizeof(flag), "%slong-help", long_pfx);
    return strcmp(token, flag) == 0;
}

/* ----------------------------------------------------------------------- */
/* Argument parsing for a command                                           */
/* ----------------------------------------------------------------------- */

static result_t parse_command_args(
    const arg_parser2_command_t* cmd,
    const arg_parser2_t* root,
    const char* path,
    int argc, char** argv,
    arg_parser2_parsed_t* parsed)
{
    const char* long_pfx  = get_prefix(root);
    const char* short_pfx = get_short_prefix(root);
    size_t long_pfx_len   = strlen(long_pfx);
    size_t short_pfx_len  = strlen(short_pfx);

    /* Temporary storage for parsed args. */
    arg_parser2_parsed_arg_t storage[MAX_PARSED_ARGS];
    uint32_t count = 0;

    /* Track which argument definitions have been fulfilled. */
    bool fulfilled[MAX_PARSED_ARGS];
    memset(fulfilled, 0, sizeof(fulfilled));

    /* Collect positional arguments in order. */
    uint32_t next_positional = 0;

    int i = 0;
    while (i < argc)
    {
        const char* token = argv[i];

        /* Intercept --help and --long-help within argument parsing for leaf commands. */
        if (is_help_flag(token, long_pfx) || is_long_help_flag(token, long_pfx))
        {
            print_command_help(cmd, path, long_pfx, short_pfx);
            parsed->arguments = NULL;
            parsed->arguments_count = 0;
            return completed_successfully;
        }

        /* Check if it's a named argument (long prefix). */
        if (str_starts_with(token, long_pfx) && long_pfx_len > 0)
        {
            const char* arg_name = token + long_pfx_len;
            const arg_parser2_arg_t* arg_def = NULL;
            uint32_t def_idx = 0;

            for (uint32_t j = 0; j < cmd->arguments_count; j++)
            {
                if (cmd->arguments[j].type == ARG_PARSER_ARG_TYPE_NAMED &&
                    cmd->arguments[j].name != NULL &&
                    strcmp(cmd->arguments[j].name, arg_name) == 0)
                {
                    arg_def = &cmd->arguments[j];
                    def_idx = j;
                    break;
                }
            }

            if (arg_def == NULL)
            {
                fprintf(stderr, "error: unknown argument '%s'\n", token);
                return invalid_argument;
            }

            if (fulfilled[def_idx] && !arg_def->multiple_allowed)
            {
                fprintf(stderr, "error: argument '%s' specified more than once\n", token);
                return invalid_argument;
            }

            if (count >= MAX_PARSED_ARGS) return insufficient_size;

            if (arg_def->value_type == ARG_PARSER_VALUE_TYPE_NONE)
            {
                /* Switch - set int32 = 1 to indicate presence. */
                storage[count].id = arg_def->id;
                storage[count].value.int32 = 1;
                storage[count].is_default = false;
                count++;
            }
            else
            {
                i++;
                if (i >= argc)
                {
                    fprintf(stderr, "error: argument '%s' requires a value\n", token);
                    return invalid_argument;
                }

                arg_parser2_value_t val;
                result_t r = parse_value(argv[i], arg_def->value_type, &val);
                if (failed(r))
                {
                    fprintf(stderr, "error: invalid value '%s' for argument '%s'\n", argv[i], token);
                    return r;
                }

                r = validate_restrictions(arg_def, val);
                if (failed(r)) return r;

                if (arg_def->custom_validator != NULL)
                {
                    r = arg_def->custom_validator(val, arg_def->value_type);
                    if (failed(r)) return r;
                }

                storage[count].id = arg_def->id;
                storage[count].value = val;
                storage[count].is_default = false;
                count++;
            }

            fulfilled[def_idx] = true;
            i++;
            continue;
        }

        /* Check if it's a short-named argument. */
        if (str_starts_with(token, short_pfx) && short_pfx_len > 0 &&
            !str_starts_with(token, long_pfx))
        {
            const char* arg_name = token + short_pfx_len;
            const arg_parser2_arg_t* arg_def = NULL;
            uint32_t def_idx = 0;

            for (uint32_t j = 0; j < cmd->arguments_count; j++)
            {
                if (cmd->arguments[j].type == ARG_PARSER_ARG_TYPE_NAMED &&
                    cmd->arguments[j].short_name != NULL &&
                    strcmp(cmd->arguments[j].short_name, arg_name) == 0)
                {
                    arg_def = &cmd->arguments[j];
                    def_idx = j;
                    break;
                }
            }

            if (arg_def == NULL)
            {
                fprintf(stderr, "error: unknown argument '%s'\n", token);
                return invalid_argument;
            }

            if (fulfilled[def_idx] && !arg_def->multiple_allowed)
            {
                fprintf(stderr, "error: argument '%s' specified more than once\n", token);
                return invalid_argument;
            }

            if (count >= MAX_PARSED_ARGS) return insufficient_size;

            if (arg_def->value_type == ARG_PARSER_VALUE_TYPE_NONE)
            {
                storage[count].id = arg_def->id;
                storage[count].value.int32 = 1;
                storage[count].is_default = false;
                count++;
            }
            else
            {
                i++;
                if (i >= argc)
                {
                    fprintf(stderr, "error: argument '%s' requires a value\n", token);
                    return invalid_argument;
                }

                arg_parser2_value_t val;
                result_t r = parse_value(argv[i], arg_def->value_type, &val);
                if (failed(r))
                {
                    fprintf(stderr, "error: invalid value '%s' for argument '%s'\n", argv[i], token);
                    return r;
                }

                r = validate_restrictions(arg_def, val);
                if (failed(r)) return r;

                if (arg_def->custom_validator != NULL)
                {
                    r = arg_def->custom_validator(val, arg_def->value_type);
                    if (failed(r)) return r;
                }

                storage[count].id = arg_def->id;
                storage[count].value = val;
                storage[count].is_default = false;
                count++;
            }

            fulfilled[def_idx] = true;
            i++;
            continue;
        }

        /* Must be a positional argument. Find the next positional arg definition. */
        const arg_parser2_arg_t* pos_def = NULL;
        uint32_t def_idx = 0;

        for (uint32_t j = 0; j < cmd->arguments_count; j++)
        {
            if (cmd->arguments[j].type == ARG_PARSER_ARG_TYPE_POSITIONAL &&
                cmd->arguments[j].position == next_positional)
            {
                pos_def = &cmd->arguments[j];
                def_idx = j;
                break;
            }
        }

        if (pos_def == NULL)
        {
            fprintf(stderr, "error: unexpected argument '%s'\n", token);
            return invalid_argument;
        }

        if (count >= MAX_PARSED_ARGS) return insufficient_size;

        arg_parser2_value_t val;
        result_t r = parse_value(token, pos_def->value_type, &val);
        if (failed(r))
        {
            fprintf(stderr, "error: invalid value '%s' for positional argument '%s'\n",
                    token, pos_def->name ? pos_def->name : "");
            return r;
        }

        r = validate_restrictions(pos_def, val);
        if (failed(r)) return r;

        if (pos_def->custom_validator != NULL)
        {
            r = pos_def->custom_validator(val, pos_def->value_type);
            if (failed(r)) return r;
        }

        storage[count].id = pos_def->id;
        storage[count].value = val;
        storage[count].is_default = false;
        count++;

        fulfilled[def_idx] = true;
        next_positional++;
        i++;
    }

    /* Fill in defaults for unfulfilled arguments. */
    for (uint32_t j = 0; j < cmd->arguments_count; j++)
    {
        if (fulfilled[j]) continue;

        const arg_parser2_arg_t* a = &cmd->arguments[j];

        if (a->required)
        {
            const char* display_name = a->name ? a->name : a->short_name;
            fprintf(stderr, "error: required argument '%s' not provided\n",
                    display_name ? display_name : "(unknown)");
            return invalid_argument;
        }

        if (a->default_value.enabled)
        {
            if (count >= MAX_PARSED_ARGS) return insufficient_size;
            storage[count].id = a->id;
            storage[count].value = a->default_value.value;
            storage[count].is_default = true;
            count++;
        }
    }

    /* Allocate the output. */
    parsed->arguments_count = count;
    if (count == 0)
    {
        parsed->arguments = NULL;
    }
    else
    {
        parsed->arguments = (arg_parser2_parsed_arg_t*)malloc(
            count * sizeof(arg_parser2_parsed_arg_t));
        if (parsed->arguments == NULL) return error;
        memcpy(parsed->arguments, storage, count * sizeof(arg_parser2_parsed_arg_t));
    }

    return ok;
}

/* ----------------------------------------------------------------------- */
/* Command dispatching                                                     */
/* ----------------------------------------------------------------------- */

static const arg_parser2_command_t* find_command(
    const arg_parser2_command_t* commands, uint32_t count, const char* name)
{
    for (uint32_t i = 0; i < count; i++)
    {
        if (strcmp(commands[i].name, name) == 0) return &commands[i];
        if (commands[i].short_name != NULL && strcmp(commands[i].short_name, name) == 0)
            return &commands[i];
    }
    return NULL;
}

static result_t dispatch(
    const arg_parser2_command_t* cmd,
    const arg_parser2_t* root,
    const char* path,
    int argc, char** argv,
    const void* context)
{
    const char* long_pfx  = get_prefix(root);
    const char* short_pfx = get_short_prefix(root);

    /* Check for --help at this command level. */
    if (argc > 0 && is_help_flag(argv[0], long_pfx))
    {
        print_command_help(cmd, path, long_pfx, short_pfx);
        return ok;
    }

    /* Check for --long-help at this command level. */
    if (argc > 0 && is_long_help_flag(argv[0], long_pfx))
    {
        if (cmd->description) fprintf(stderr, "%s", cmd->description);
        fprintf(stderr, "\n\n");
        for (uint32_t i = 0; i < cmd->commands_count; i++)
        {
            if (i > 0) fprintf(stderr, "\n");
            print_long_help_cmd(&cmd->commands[i], long_pfx, short_pfx, 0);
        }
        if (cmd->arguments_count > 0)
        {
            fprintf(stderr, "\nArguments:\n");
            for (uint32_t i = 0; i < cmd->arguments_count; i++)
            {
                const arg_parser2_arg_t* a = &cmd->arguments[i];
                if (a->type == ARG_PARSER_ARG_TYPE_NAMED)
                {
                    fprintf(stderr, "  ");
                    if (a->short_name) fprintf(stderr, "%s%s", short_pfx, a->short_name);
                    if (a->short_name && a->name) fprintf(stderr, ", ");
                    if (a->name) fprintf(stderr, "%s%s", long_pfx, a->name);
                    if (a->value_type != ARG_PARSER_VALUE_TYPE_NONE)
                        fprintf(stderr, " <value>");
                    if (a->description) fprintf(stderr, "\t%s", a->description);
                    if (a->required) fprintf(stderr, " (required)");
                    fprintf(stderr, "\n");
                }
                else
                {
                    fprintf(stderr, "  <%s>", a->name ? a->name : "arg");
                    if (a->description) fprintf(stderr, "\t%s", a->description);
                    if (a->required) fprintf(stderr, " (required)");
                    fprintf(stderr, "\n");
                }
            }
        }
        fprintf(stderr, "\nOptions:\n");
        fprintf(stderr, "  %shelp             Show help for this command\n", long_pfx);
        fprintf(stderr, "  %slong-help        Show detailed help for all subcommands\n", long_pfx);
        return ok;
    }

    /* If the command has subcommands, try to match the next token. */
    if (cmd->commands_count > 0 && argc > 0)
    {
        const arg_parser2_command_t* sub = find_command(cmd->commands, cmd->commands_count, argv[0]);
        if (sub != NULL)
        {
            char sub_path[256];
            snprintf(sub_path, sizeof(sub_path), "%s %s", path, sub->name);
            return dispatch(sub, root, sub_path, argc - 1, argv + 1, context);
        }
    }

    /* If subcommands exist but nothing matched and no handler, error. */
    if (cmd->commands_count > 0 && cmd->handler == NULL)
    {
        if (argc == 0)
        {
            fprintf(stderr, "error: '%s' requires a subcommand\n", cmd->name);
        }
        else
        {
            fprintf(stderr, "error: unknown subcommand '%s' for '%s'\n", argv[0], cmd->name);
        }
        print_command_help(cmd, path, long_pfx, short_pfx);
        return invalid_argument;
    }

    /* Parse arguments and call handler. */
    if (cmd->handler == NULL)
    {
        fprintf(stderr, "error: command '%s' has no handler\n", cmd->name);
        return invalid_state;
    }

    arg_parser2_parsed_t parsed = { 0 };
    result_t r = parse_command_args(cmd, root, path, argc, argv, &parsed);
    if (failed(r)) return r;

    /* Help was printed inside parse_command_args; do not call handler. */
    if (r == completed_successfully) return ok;

    r = cmd->handler(&parsed, context);

    free(parsed.arguments);
    return r;
}

/* ----------------------------------------------------------------------- */
/* Public API                                                              */
/* ----------------------------------------------------------------------- */

result_t arg_parser2_process_args(const arg_parser2_t* root, int argc, char** argv)
{
    if (root == NULL) return invalid_argument;

    /* Extract the base program name from argv[0]. */
    const char* prog = "program";
    if (argc > 0 && argv[0] != NULL)
    {
        const char* slash = strrchr(argv[0], '/');
        prog = slash ? slash + 1 : argv[0];
    }

    /* Skip program name. */
    if (argc > 0)
    {
        argc--;
        argv++;
    }

    const char* long_pfx = get_prefix(root);

    if (argc == 0 || root->commands_count == 0)
    {
        print_root_help(root);
        return (argc == 0) ? invalid_argument : ok;
    }

    /* Check for --help and --long-help at root level. */
    if (is_help_flag(argv[0], long_pfx))
    {
        print_root_help(root);
        return ok;
    }

    if (is_long_help_flag(argv[0], long_pfx))
    {
        print_long_help(root);
        return ok;
    }

    const arg_parser2_command_t* cmd = find_command(root->commands, root->commands_count, argv[0]);
    if (cmd == NULL)
    {
        fprintf(stderr, "error: unknown command '%s'\n", argv[0]);
        print_root_help(root);
        return invalid_argument;
    }

    char initial_path[256];
    snprintf(initial_path, sizeof(initial_path), "%s %s", prog, cmd->name);
    return dispatch(cmd, root, initial_path, argc - 1, argv + 1, NULL);
}

arg_parser2_parsed_arg_t* arg_parser2_get_argument_by_id(
    arg_parser2_parsed_t* parsed, uint32_t id)
{
    if (parsed == NULL || parsed->arguments == NULL) return NULL;

    for (uint32_t i = 0; i < parsed->arguments_count; i++)
    {
        if (parsed->arguments[i].id == id)
        {
            return &parsed->arguments[i];
        }
    }
    return NULL;
}
