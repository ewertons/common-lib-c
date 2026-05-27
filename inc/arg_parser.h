#ifndef ARG_PARSER_H
#define ARG_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include <niceties.h>

#define ARG_PARSER_MAX_COMMANDS     16
#define ARG_PARSER_MAX_SUBCOMMANDS  16

typedef result_t (*arg_parser_handler_t)(int argc, char** argv);

typedef struct arg_parser_command
{
    const char*                name;
    arg_parser_handler_t       handler;
    struct arg_parser_command*  subcommands[ARG_PARSER_MAX_SUBCOMMANDS];
    uint32_t                   subcommand_count;
} arg_parser_command_t;

typedef struct arg_parser
{
    arg_parser_command_t*  commands[ARG_PARSER_MAX_COMMANDS];
    uint32_t               command_count;
} arg_parser_t;

void     arg_parser_init(arg_parser_t* parser);
void     arg_parser_command_init(arg_parser_command_t* cmd, const char* name, arg_parser_handler_t handler);
result_t arg_parser_add_subcommand(arg_parser_command_t* parent, arg_parser_command_t* child);
result_t arg_parser_add_command(arg_parser_t* parser, arg_parser_command_t* cmd);
result_t arg_parser_process_args(arg_parser_t* parser, int argc, char** argv);

#endif /* ARG_PARSER_H */
