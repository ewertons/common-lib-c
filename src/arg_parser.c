#include "arg_parser.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void arg_parser_init(arg_parser_t* parser)
{
    if (parser == NULL) return;
    parser->command_count = 0;
    memset(parser->commands, 0, sizeof(parser->commands));
}

void arg_parser_command_init(arg_parser_command_t* cmd, const char* name, arg_parser_handler_t handler)
{
    if (cmd == NULL) return;
    cmd->name = name;
    cmd->handler = handler;
    cmd->subcommand_count = 0;
    memset(cmd->subcommands, 0, sizeof(cmd->subcommands));
}

result_t arg_parser_add_subcommand(arg_parser_command_t* parent, arg_parser_command_t* child)
{
    if (parent == NULL || child == NULL) return invalid_argument;
    if (parent->subcommand_count >= ARG_PARSER_MAX_SUBCOMMANDS) return insufficient_size;

    parent->subcommands[parent->subcommand_count++] = child;
    return ok;
}

result_t arg_parser_add_command(arg_parser_t* parser, arg_parser_command_t* cmd)
{
    if (parser == NULL || cmd == NULL) return invalid_argument;
    if (parser->command_count >= ARG_PARSER_MAX_COMMANDS) return insufficient_size;

    parser->commands[parser->command_count++] = cmd;
    return ok;
}

static void print_usage_for_command(arg_parser_command_t* cmd, const char* prefix)
{
    if (cmd->subcommand_count > 0)
    {
        fprintf(stderr, "usage: %s %s <subcommand>\n", prefix, cmd->name);
        fprintf(stderr, "available subcommands:\n");
        for (uint32_t i = 0; i < cmd->subcommand_count; i++)
        {
            fprintf(stderr, "  %s\n", cmd->subcommands[i]->name);
        }
    }
    else
    {
        fprintf(stderr, "usage: %s %s\n", prefix, cmd->name);
    }
}

static result_t dispatch_command(arg_parser_command_t* cmd, int argc, char** argv, const char* prog)
{
    /* If there are subcommands, we need to match one */
    if (cmd->subcommand_count > 0)
    {
        if (argc < 1)
        {
            print_usage_for_command(cmd, prog);
            exit(2);
        }

        for (uint32_t i = 0; i < cmd->subcommand_count; i++)
        {
            if (strcmp(argv[0], cmd->subcommands[i]->name) == 0)
            {
                return dispatch_command(cmd->subcommands[i], argc - 1, argv + 1, prog);
            }
        }

        fprintf(stderr, "unknown subcommand '%s' for '%s'\n", argv[0], cmd->name);
        print_usage_for_command(cmd, prog);
        exit(2);
    }

    /* Leaf command: must have a handler */
    if (cmd->handler != NULL)
    {
        return cmd->handler(argc, argv);
    }

    fprintf(stderr, "command '%s' requires a subcommand\n", cmd->name);
    exit(2);
}

result_t arg_parser_process_args(arg_parser_t* parser, int argc, char** argv)
{
    if (parser == NULL) return invalid_argument;

    const char* prog = (argc > 0) ? argv[0] : "program";

    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <command>\n", prog);
        fprintf(stderr, "available commands:\n");
        for (uint32_t i = 0; i < parser->command_count; i++)
        {
            fprintf(stderr, "  %s\n", parser->commands[i]->name);
        }
        exit(2);
    }

    for (uint32_t i = 0; i < parser->command_count; i++)
    {
        if (strcmp(argv[1], parser->commands[i]->name) == 0)
        {
            return dispatch_command(parser->commands[i], argc - 2, argv + 2, prog);
        }
    }

    fprintf(stderr, "unknown command '%s'\n", argv[1]);
    fprintf(stderr, "available commands:\n");
    for (uint32_t i = 0; i < parser->command_count; i++)
    {
        fprintf(stderr, "  %s\n", parser->commands[i]->name);
    }
    exit(2);
}