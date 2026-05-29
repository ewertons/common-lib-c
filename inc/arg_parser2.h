#ifndef ARG_PARSER2_H
#define ARG_PARSER2_H

#include <stdbool.h>
#include <stdint.h>
#include <niceties.h>

/* ----------------------------------------------------------------------- */
/* Value types                                                             */
/* ----------------------------------------------------------------------- */

typedef enum arg_parser2_value_type
{
    ARG_PARSER_VALUE_TYPE_NONE = 0,   /* Switch, no value follows. */
    ARG_PARSER_VALUE_TYPE_INT32,
    ARG_PARSER_VALUE_TYPE_UINT32,
    ARG_PARSER_VALUE_TYPE_STRING,
    ARG_PARSER_VALUE_TYPE_CHAR,
    ARG_PARSER_VALUE_TYPE_DOUBLE
} arg_parser2_value_type_t;

/* ----------------------------------------------------------------------- */
/* Value union                                                             */
/* ----------------------------------------------------------------------- */

typedef union arg_parser2_value
{
    int32_t      int32;
    uint32_t     uint32;
    const char*  string;
    char         character;
    double       dbl;
} arg_parser2_value_t;

/* ----------------------------------------------------------------------- */
/* Argument type (named vs positional)                                     */
/* ----------------------------------------------------------------------- */

typedef enum arg_parser2_arg_type
{
    ARG_PARSER_ARG_TYPE_NAMED,
    ARG_PARSER_ARG_TYPE_POSITIONAL
} arg_parser2_arg_type_t;

/* ----------------------------------------------------------------------- */
/* Default value                                                           */
/* ----------------------------------------------------------------------- */

typedef struct arg_parser2_default_value
{
    bool                 enabled;
    arg_parser2_value_t  value;
} arg_parser2_default_value_t;

/* ----------------------------------------------------------------------- */
/* Value restrictions                                                      */
/* ----------------------------------------------------------------------- */

typedef struct arg_parser2_range
{
    bool    enabled;
    double  min;
    double  max;
    double  step;
} arg_parser2_range_t;

typedef struct arg_parser2_allowed_values
{
    union
    {
        const char**     string;
        const int32_t*   int32;
        const uint32_t*  uint32;
        const double*    dbl;
    } values;
    uint32_t count;
} arg_parser2_allowed_values_t;

typedef struct arg_parser2_value_restrictions
{
    bool                           enabled;
    arg_parser2_range_t            range;
    arg_parser2_allowed_values_t   allowed;
} arg_parser2_value_restrictions_t;

/* ----------------------------------------------------------------------- */
/* Custom validator                                                        */
/* ----------------------------------------------------------------------- */

typedef result_t (*arg_parser2_arg_value_validator_t)(
    arg_parser2_value_t value, arg_parser2_value_type_t type);

/* ----------------------------------------------------------------------- */
/* Argument definition                                                     */
/* ----------------------------------------------------------------------- */

typedef struct arg_parser2_arg
{
    uint32_t                            id;
    arg_parser2_arg_type_t              type;
    bool                                required;
    bool                                multiple_allowed; /* Only valid for named arguments. */
    uint32_t                            position;         /* Position among positional args; ignored for named. */
    const char*                         description;
    const char*                         name;             /* Long name (can be NULL if short_name is set). */
    const char*                         short_name;       /* Short name (can be NULL if name is set). */
    arg_parser2_value_type_t            value_type;
    arg_parser2_default_value_t         default_value;
    arg_parser2_value_restrictions_t    value_restrictions;
    arg_parser2_arg_value_validator_t   custom_validator;
} arg_parser2_arg_t;

/* ----------------------------------------------------------------------- */
/* Parsed argument (output)                                                */
/* ----------------------------------------------------------------------- */

typedef struct arg_parser2_parsed_arg
{
    uint32_t             id;
    arg_parser2_value_t  value;
    bool                 is_default;
} arg_parser2_parsed_arg_t;

typedef struct arg_parser2_parsed
{
    arg_parser2_parsed_arg_t*  arguments;
    uint32_t                   arguments_count;
} arg_parser2_parsed_t;

/* ----------------------------------------------------------------------- */
/* Handler                                                                 */
/* ----------------------------------------------------------------------- */

typedef result_t (*arg_parser2_handler_t)(
    arg_parser2_parsed_t* parsed, const void* context);

/* ----------------------------------------------------------------------- */
/* Command definition                                                      */
/* ----------------------------------------------------------------------- */

typedef struct arg_parser2_command
{
    const char*                  description;
    const char*                  name;
    const char*                  short_name;    /* NULL means no short name. */
    const struct arg_parser2_command*  commands;
    uint32_t                     commands_count;
    const arg_parser2_arg_t*     arguments;
    uint32_t                     arguments_count;
    arg_parser2_handler_t        handler;       /* NULL means subcommands are required. */
} arg_parser2_command_t;

/* ----------------------------------------------------------------------- */
/* Root parser definition                                                  */
/* ----------------------------------------------------------------------- */

typedef struct arg_parser2
{
    const char*                     description;
    const arg_parser2_command_t*    commands;
    uint32_t                        commands_count;
    const arg_parser2_arg_t*        arguments;
    uint32_t                        arguments_count;
    const char*                     arguments_prefix;        /* NULL to use default "--". */
    const char*                     short_arguments_prefix;  /* NULL to use default "-". */
} arg_parser2_t;

/* ----------------------------------------------------------------------- */
/* API                                                                     */
/* ----------------------------------------------------------------------- */

/**
 * @brief   Parse command-line arguments according to the parser definition and
 *          invoke the matching command handler.
 *
 * @param   root    The root parser definition.
 * @param   argc    Argument count (from main).
 * @param   argv    Argument vector (from main).
 * @return  The result returned by the matched command handler, or an error
 *          if parsing fails.
 */
result_t arg_parser2_process_args(const arg_parser2_t* root, int argc, char** argv);

/**
 * @brief   Retrieve a parsed argument by its id from the parsed result.
 *
 * @param   parsed  The parsed arguments structure passed to the handler.
 * @param   id      The unique id of the argument to look up.
 * @return  Pointer to the parsed argument, or NULL if not found.
 */
arg_parser2_parsed_arg_t* arg_parser2_get_argument_by_id(
    arg_parser2_parsed_t* parsed, uint32_t id);

#endif /* ARG_PARSER2_H */
