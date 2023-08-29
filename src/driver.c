#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "layec/context.h"
#include "layec/source.h"
#include "layec/string.h"
#include "layec/vector.h"
#include "layec/c/lexer.h"
#include "layec/laye/lexer.h"

#define VERSION_STRING "v0.2.1"

const char* usage_text =
"layec " VERSION_STRING "\n"
"\n"
"Usage: layec [options] file...\n"
"\n"
"Options:\n"
"--help             Display this help message, then exit\n"
"-I <dir>           Add directory to the end of the list of include search paths\n"
"--parse-only       Only run the parser for input source files\n"
"--print-ast        Print parse trees for input source files\n"
"--verbose          Enable verbose output\n"
"--version          Print the compiler name and version information, then exit\n"
"\n";

static bool parse_args(layec_context* context, int argc, char** argv);
static void arg_shift(int* argc, char*** argv);
static void handle_input_file(layec_context* context, int source_id);

int main(int argc, char** argv)
{
    if (argc <= 1) goto print_usage;

    layec_context* context = layec_context_create();
    assert(context);

    arg_shift(&argc, &argv);
    if (!parse_args(context, argc, argv) || context->help)
    {
    print_usage:
        printf("%s", usage_text);
        return 0;
    }

    if (context->version)
    {
        printf("layec compiler " VERSION_STRING "\n");
        return 0;
    }

    for (long long i = 0; i < vector_count(context->input_file_names); i++)
    {
        layec_string_view file_path = context->input_file_names[i];
        
        int source_id = layec_context_get_or_add_source_buffer_from_file(context, file_path);
        if (source_id <= 0)
        {
            printf("could not read included file '%.*s'", LAYEC_STRING_VIEW_EXPAND(file_path));
            layec_context_destroy(context);
            return 1;
        }

        handle_input_file(context, source_id);
    }

    layec_context_destroy(context);
    return 0;
}

static void arg_shift(int* argc, char*** argv)
{
    (*argc)--;
    (*argv)++;
}

static bool parse_args(layec_context* context, int argc, char** argv)
{
    if (argc == 0) return true;

    const char* option = argv[0];
    if (0 == strcmp(option, "-I"))
    {
        arg_shift(&argc, &argv);
        if (argc == 0)
        {
            printf("'-I' reqires a value\n");
            return false;
        }

        option = argv[0];
        vector_push(context->include_dirs, layec_string_view_create(option, (long long)strlen(option)));
    }
    else if (0 == strcmp(option, "--parse-only"))
        context->parse_only = true;
    else if (0 == strcmp(option, "--print-ast"))
        context->print_ast = true;
    else if (0 == strcmp(option, "--help"))
        context->help = true;
    else if (0 == strcmp(option, "--version"))
        context->version = true;
    else if (0 == strcmp(option, "--verbose"))
        context->verbose = true;
    else vector_push(context->input_file_names, layec_string_view_create(option, (long long)strlen(option)));

    arg_shift(&argc, &argv);
    return parse_args(context, argc, argv);
}

static void handle_input_file(layec_context* context, int source_id)
{
    layec_source_buffer source_buffer = layec_context_get_source_buffer(context, source_id);
    layec_string_view file_name = source_buffer.name;
    
    if (layec_string_view_ends_with_cstring(file_name, ".c"))
    {
        layec_c_translation_unit* tu = calloc(1, sizeof *tu);
        layec_c_token_buffer token_buffer = layec_c_get_tokens(context, tu, source_id);

        if (context->print_ast)
        {
            printf("----- tokens in source file %.*s\n", LAYEC_STRING_VIEW_EXPAND(file_name));
            for (long long i = 0; i < vector_count(token_buffer.semantic_tokens); i++)
            {
                layec_c_token token = token_buffer.semantic_tokens[i];
                layec_location_print(context, token.location);
                printf(": ");
                layec_c_token_print(context, token);
                printf("\n");
            }

            printf("\n");
        }

        layec_c_token_buffer_destroy(&token_buffer);
        layec_c_translation_unit_destroy(tu);
    }
    else if (layec_string_view_ends_with_cstring(file_name, ".laye"))
    {
        layec_laye_token_buffer token_buffer = layec_laye_get_tokens(context, source_id);

        if (context->print_ast)
        {
            printf("----- tokens in source file %.*s\n", LAYEC_STRING_VIEW_EXPAND(file_name));
            for (long long i = 0; i < vector_count(token_buffer.tokens); i++)
            {
                layec_laye_token token = token_buffer.tokens[i];
                layec_location_print(context, token.location);
                printf(": ");
                layec_laye_token_print(context, token);
                printf("\n");
            }
            
            printf("\n");
        }

        layec_laye_token_buffer_destroy(&token_buffer);
    }
    else
    {
        printf("Unknown file type\n");
    }
}
