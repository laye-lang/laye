#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "layec/string.h"
#include "layec/vector.h"
#include "layec/c/lexer.h"

typedef struct layec_c_lexer layec_c_lexer;
typedef struct layec_c_macro_expansion layec_c_macro_expansion;
typedef struct keyword_info keyword_info;

struct layec_c_lexer
{
    layec_context* context;
    layec_c_translation_unit* tu;
    int source_id;
    layec_source_buffer source_buffer;

    const char* cur;
    const char* end;

    const char* current_char_location;
    int current_char;
    bool at_start_of_line;
    bool is_in_preprocessor;
    bool is_in_include;

    vector(layec_c_macro_expansion*) macro_expansions;
};

struct layec_c_macro_expansion
{
    layec_c_macro_def* def;
    long long body_position;
    vector(vector(layec_c_token)) args;
    long long arg_index; // set to -1 when not expanding an argument
    long long arg_position;
};

static layec_c_macro_expansion* layec_c_macro_expansion_create(layec_c_macro_def* def, vector(vector(layec_c_token)) args, long long arg_index)
{
    layec_c_macro_expansion* expansion = calloc(1, sizeof *expansion);
    expansion->def = def;
    expansion->args = args;
    expansion->arg_index = arg_index;
    return expansion;
}

static void layec_c_macro_expansion_destroy(layec_c_macro_expansion* expansion)
{
    for (long long i = 0; i < vector_count(expansion->args); i++)
        vector_free(expansion->args[i]);
    vector_free(expansion->args);
    *expansion = (layec_c_macro_expansion){0};
    free(expansion);
}

struct keyword_info
{
    const char* name;
    layec_c_token_kind kind;
};

struct keyword_info c89_keywords[] = {
    {"auto", LAYEC_CTK_AUTO},
    {"break", LAYEC_CTK_BREAK},
    {"case", LAYEC_CTK_CASE},
    {"char", LAYEC_CTK_CHAR},
    {"const", LAYEC_CTK_CONST},
    {"continue", LAYEC_CTK_CONTINUE},
    {"default", LAYEC_CTK_DEFAULT},
    {"do", LAYEC_CTK_DO},
    {"double", LAYEC_CTK_DOUBLE},
    {"else", LAYEC_CTK_ELSE},
    {"enum", LAYEC_CTK_ENUM},
    {"extern", LAYEC_CTK_EXTERN},
    {"float", LAYEC_CTK_FLOAT},
    {"for", LAYEC_CTK_FOR},
    {"goto", LAYEC_CTK_GOTO},
    {"if", LAYEC_CTK_IF},
    {"int", LAYEC_CTK_INT},
    {"long", LAYEC_CTK_LONG},
    {"register", LAYEC_CTK_REGISTER},
    {"return", LAYEC_CTK_RETURN},
    {"short", LAYEC_CTK_SHORT},
    {"signed", LAYEC_CTK_SIGNED},
    {"sizeof", LAYEC_CTK_SIZEOF},
    {"static", LAYEC_CTK_STATIC},
    {"struct", LAYEC_CTK_STRUCT},
    {"switch", LAYEC_CTK_SWITCH},
    {"typedef", LAYEC_CTK_TYPEDEF},
    {"union", LAYEC_CTK_UNION},
    {"unsigned", LAYEC_CTK_UNSIGNED},
    {"void", LAYEC_CTK_VOID},
    {"volatile", LAYEC_CTK_VOLATILE},
    {"while", LAYEC_CTK_WHILE},
    {0},
};

static layec_location layec_c_lexer_get_location(layec_c_lexer *lexer)
{
    return (layec_location)
    {
        .source_id = lexer->source_id,
        .offset = lexer->current_char_location - lexer->source_buffer.text.data,
        .length = 1,
    };
}

static bool layec_c_lexer_at_eof(layec_c_lexer* lexer);
static void layec_c_lexer_advance(layec_c_lexer* lexer, bool allow_comments);
static int layec_c_lexer_peek_no_process(layec_c_lexer* lexer, int ahead);
static void layec_c_lexer_read_token(layec_c_lexer* lexer, layec_c_token* out_token);

layec_c_token_buffer layec_c_get_tokens(layec_context* context, layec_c_translation_unit* tu, int source_id)
{
    assert(context);

    layec_c_lexer lexer = {
        .context = context,
        .tu = tu,
        .source_id = source_id,
        .at_start_of_line = true,
    };

    lexer.source_buffer = layec_context_get_source_buffer(context, source_id);
    lexer.cur = lexer.source_buffer.text.data;
    lexer.end = lexer.cur + lexer.source_buffer.text.length;

    layec_c_lexer_advance(&lexer, true);

    layec_c_token_buffer token_buffer = {0};
    for (;;)
    {
        layec_c_token token = {0};
        layec_c_lexer_read_token(&lexer, &token);
        if (token.kind == LAYEC_CTK_EOF) break;
        vector_push(token_buffer.semantic_tokens, token);
    }

    vector_free(lexer.macro_expansions);
    return token_buffer;
}

static void layec_c_lexer_eat_white_space(layec_c_lexer* lexer)
{
    while (is_space(lexer->current_char))
    {
        if (lexer->is_in_preprocessor && lexer->current_char == '\n')
            break;
        layec_c_lexer_advance(lexer, true);
    }
}

static int layec_c_lexer_read_escape_sequence(layec_c_lexer* lexer, bool allow_comments)
{
    assert(lexer->current_char == '\\' && layec_c_lexer_peek_no_process(lexer, 1) != '\n' && layec_c_lexer_peek_no_process(lexer, 1) != '\r');
    layec_c_lexer_advance(lexer, allow_comments);
    if (layec_c_lexer_at_eof(lexer))
    {
        layec_location location = layec_c_lexer_get_location(lexer);
        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
            location);
        printf("End of file reached when lexing escape sequence");
        layec_c_lexer_advance(lexer, true);
        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
            location);
        return 0;
    }

    switch (lexer->current_char)
    {
        default:
        {
            layec_location location = layec_c_lexer_get_location(lexer);
            layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                location);
            printf("Unrecognized escape sequence");
            layec_c_lexer_advance(lexer, true);
            layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                location);
            return 0;
        }

        case '0': return '\0';
        case 'a': return '\a';
        case 'b': return '\b';
        case 'f': return '\f';
        case 'n': return '\n';
        case 'r': return '\r';
        case 't': return '\t';
        case 'v': return '\v';
    }
}

static void layec_c_lexer_read_token_no_preprocess(layec_c_lexer* lexer, layec_c_token* out_token)
{
    layec_c_lexer_eat_white_space(lexer);
    if (layec_c_lexer_at_eof(lexer))
    {
        out_token->kind = LAYEC_CTK_EOF;
        return;
    }

    layec_location start_location = layec_c_lexer_get_location(lexer);
    out_token->location = start_location;

    int cur = lexer->current_char;
    switch (cur)
    {
        case '\n':
        {
            assert(lexer->is_in_preprocessor);
            out_token->kind = (layec_c_token_kind)'\n';
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char != 0 && lexer->cur < lexer->end) assert(lexer->at_start_of_line);
        } break;

        case '#':
        {
            if (!lexer->is_in_preprocessor)
                goto default_case;
            
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '#')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_PP_HASH_HASH;
            }
            else out_token->kind = '#';
        } break;

        case '~': case '?':
        case '(': case ')':
        case '[': case ']':
        case '{': case '}':
        case ';': case ':':
        case ',':
        {
            out_token->kind = (layec_c_token_kind)cur;
            layec_c_lexer_advance(lexer, true);
        } break;

        case '.':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '.' && layec_c_lexer_peek_no_process(lexer, 1) == '.')
            {
                layec_c_lexer_advance(lexer, true);
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_TRIPLE_DOT;
            }
            else out_token->kind = '.';
        } break;

        case '+':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '+')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_PLUS_PLUS;
            }
            else if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_PLUS_EQUAL;
            }
            else out_token->kind = '+';
        } break;

        case '-':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '-')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_MINUS_MINUS;
            }
            else if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_MINUS_EQUAL;
            }
            else out_token->kind = '-';
        } break;

        case '*':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_STAR_EQUAL;
            }
            else out_token->kind = '*';
        } break;

        case '/':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_SLASH_EQUAL;
            }
            else out_token->kind = '/';
        } break;

        case '%':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_PERCENT_EQUAL;
            }
            else out_token->kind = '%';
        } break;

        case '&':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '&')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_AMPERSAND_AMPERSAND;
            }
            else if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_AMPERSAND_EQUAL;
            }
            else out_token->kind = '&';
        } break;

        case '|':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '|')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_PIPE_PIPE;
            }
            else if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_PIPE_EQUAL;
            }
            else out_token->kind = '|';
        } break;

        case '^':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_CARET_EQUAL;
            }
            else out_token->kind = '^';
        } break;

        case '<':
        {
            if (lexer->is_in_include)
                goto parse_string_literal;

            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '<')
            {
                layec_c_lexer_advance(lexer, true);
                if (lexer->current_char == '=')
                {
                    layec_c_lexer_advance(lexer, true);
                    out_token->kind = LAYEC_CTK_LESS_LESS_EQUAL;
                }
                else out_token->kind = LAYEC_CTK_LESS_LESS;
            }
            else if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_LESS_EQUAL;
            }
            else out_token->kind = '<';
        } break;

        case '>':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '>')
            {
                layec_c_lexer_advance(lexer, true);
                if (lexer->current_char == '=')
                {
                    layec_c_lexer_advance(lexer, true);
                    out_token->kind = LAYEC_CTK_GREATER_GREATER_EQUAL;
                }
                else out_token->kind = LAYEC_CTK_GREATER_GREATER;
            }
            else if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_GREATER_EQUAL;
            }
            else out_token->kind = '>';
        } break;

        case '=':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_EQUAL_EQUAL;
            }
            else out_token->kind = '=';
        } break;

        case '!':
        {
            layec_c_lexer_advance(lexer, true);
            if (lexer->current_char == '=')
            {
                layec_c_lexer_advance(lexer, true);
                out_token->kind = LAYEC_CTK_BANG_EQUAL;
            }
            else out_token->kind = '!';
        } break;

        case '\'':
        {
            layec_c_lexer_advance(lexer, false);
            out_token->kind = LAYEC_CTK_LIT_CHAR;
            if (lexer->current_char == '\'')
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("quoted character should contain at least one character");
                layec_c_lexer_advance(lexer, false);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
            else
            {
                if (lexer->current_char == '\\')
                    out_token->int_value = layec_c_lexer_read_escape_sequence(lexer, false);
                else
                {
                    out_token->int_value = lexer->current_char;
                    layec_c_lexer_advance(lexer, false);
                }
            }

            if (lexer->current_char != '\'')
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("missing closing quote");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
            else layec_c_lexer_advance(lexer, false);
        } break;

        case '"':
        {
        parse_string_literal:;
            char end_delim = lexer->current_char == '"' ? '"' : '>';
            layec_string_builder builder = {0};

            layec_c_lexer_advance(lexer, false);
            out_token->kind = LAYEC_CTK_LIT_STRING;

            while (lexer->current_char != end_delim)
            {
                if (layec_c_lexer_at_eof(lexer))
                {
                    layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                        start_location);
                    printf("unfinished string");
                    layec_c_lexer_advance(lexer, false);
                    layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                        start_location);
                    goto finish_token;
                }
                else if (lexer->current_char == '\\' && !lexer->is_in_include)
                    layec_string_builder_append_rune(&builder, layec_c_lexer_read_escape_sequence(lexer, false));
                else
                {
                    layec_string_builder_append_rune(&builder, lexer->current_char);
                    layec_c_lexer_advance(lexer, false);
                }
            }

            if (lexer->current_char != end_delim)
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("missing closing quote");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
            else layec_c_lexer_advance(lexer, false);

            layec_string_view string_value = layec_context_intern_string_builder(lexer->context, builder);
            layec_string_builder_destroy(&builder);
            
            out_token->string_value = string_value;
        } break;
        
        case '0': case '1': case '2': case '3': case '4': 
        case '5': case '6': case '7': case '8': case '9':
        {
            int radix = 10;
            if (lexer->current_char == '0')
            {
                layec_c_lexer_advance(lexer, true);
                if (lexer->current_char == 'x' || lexer->current_char == 'X')
                {
                    radix = 16;
                    layec_c_lexer_advance(lexer, true);
                }
                else if (lexer->current_char == 'b' || lexer->current_char == 'B')
                {
                    radix = 2;
                    layec_c_lexer_advance(lexer, true);
                }
                else radix = 8;
            }

            while (radix == 16 ? is_hex_digit(lexer->current_char) : is_digit(lexer->current_char))
                layec_c_lexer_advance(lexer, true);

            // TODO(local): integer suffixes
            // u l ll ull llu lu f ld

            layec_string_view suffix_view = {0};
            if (is_alpha_numeric(lexer->current_char))
            {
                layec_location suffix_location = layec_c_lexer_get_location(lexer);
                do layec_c_lexer_advance(lexer, true);
                while (is_alpha_numeric(lexer->current_char));
                layec_location suffix_end_location = layec_c_lexer_get_location(lexer);
                out_token->string_value = layec_context_intern_string_view(lexer->context,
                    layec_string_view_slice(lexer->source_buffer.text, suffix_location.offset,
                        suffix_end_location.offset - suffix_location.offset));
            }

            if (suffix_view.length != 0)
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("Integer literal suffixes are not yet supported");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
            
            out_token->kind = LAYEC_CTK_LIT_INT;
        } break;

        case 'a': case 'b': case 'c': case 'd': case 'e': 
        case 'f': case 'g': case 'h': case 'i': case 'j': 
        case 'k': case 'l': case 'm': case 'n': case 'o': 
        case 'p': case 'q': case 'r': case 's': case 't': 
        case 'u': case 'v': case 'w': case 'x': case 'y': 
        case 'z':
        
        case 'A': case 'B': case 'C': case 'D': case 'E': 
        case 'F': case 'G': case 'H': case 'I': case 'J': 
        case 'K': case 'L': case 'M': case 'N': case 'O': 
        case 'P': case 'Q': case 'R': case 'S': case 'T': 
        case 'U': case 'V': case 'W': case 'X': case 'Y': 
        case 'Z':

        case '_':
        {
            out_token->kind = LAYEC_CTK_IDENT;

            while (is_alpha_numeric(lexer->current_char) || lexer->current_char == '_')
                layec_c_lexer_advance(lexer, true);

            layec_location ident_end_location = layec_c_lexer_get_location(lexer);
            out_token->string_value = layec_context_intern_string_view(lexer->context,
                layec_string_view_slice(lexer->source_buffer.text, start_location.offset,
                    ident_end_location.offset - start_location.offset));
        } break;

        default:
        {
        default_case:;
            layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                start_location);
            printf("Invalid character in source text");
            layec_c_lexer_advance(lexer, true);
            layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                start_location);
        } break;
    }

finish_token:;
    layec_location end_location = layec_c_lexer_get_location(lexer);
    out_token->location.length = end_location.offset - start_location.offset;
}

static layec_c_macro_def* layec_c_lexer_lookup_macro_def(layec_c_lexer* lexer, layec_string_view macro_name)
{
    for (long long i = 0; i < vector_count(lexer->tu->macro_defs); i++)
    {
        layec_c_macro_def* def = lexer->tu->macro_defs[i];
        if (macro_name.length != def->name.length)
            continue;
        
        if (0 == strncmp(macro_name.data, def->name.data, (unsigned long long)macro_name.length))
            return def;
    }

    return NULL;
}

static void layec_c_lexer_handle_preprocessor_directive(layec_c_lexer* lexer);

static bool layec_c_lexer_pp_try_expand_token(layec_c_lexer* lexer, layec_c_token token, long long* token_pos, vector(layec_c_token)* next_tokens, layec_c_token* out_token)
{
    assert(lexer);
    assert(token.kind == LAYEC_CTK_IDENT);

    layec_c_macro_def* found_macro_def = layec_c_lexer_lookup_macro_def(lexer, token.string_value);
    if (!found_macro_def) return false;
    
    vector(vector(layec_c_token)) args = NULL;
    if (found_macro_def->has_params)
    {
        layec_c_lexer lexer_cache = *lexer;

        layec_c_token arg_token = {0};
        if (token_pos)
        {
            assert(*next_tokens);
            if (*token_pos < vector_count(*next_tokens))
                arg_token = (*next_tokens)[*token_pos];
        }
        else layec_c_lexer_read_token_no_preprocess(lexer, &arg_token);

        if (arg_token.kind != '(')
        {
            *lexer = lexer_cache;
            return false;
        }
            
        if (token_pos) (*token_pos)++;

        vector(layec_c_token) current_arg = NULL;
        int paren_nesting = 0;

        for (;;)
        {
            if ((token_pos && *token_pos >= vector_count(*next_tokens)) || layec_c_lexer_at_eof(lexer))
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    arg_token.location);
                printf("expected ')' in macro argument list");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    arg_token.location);
                    
                out_token->kind = LAYEC_CTK_EOF;
                break;
            }

            if (token_pos)
            {
                arg_token = (*next_tokens)[*token_pos];
                (*token_pos)++;
            }
            else layec_c_lexer_read_token_no_preprocess(lexer, &arg_token);

            if (arg_token.kind == ')')
            {
                if (paren_nesting == 0)
                {
                    vector_push(args, current_arg);
                    current_arg = NULL;
                    break;
                }
                else paren_nesting--;
            }
            else if (arg_token.kind == ',' && paren_nesting == 0)
            {
                vector_push(args, current_arg);
                current_arg = NULL;
                continue;
            }
            else if (arg_token.kind == '(')
                paren_nesting++;
            else if (arg_token.kind == LAYEC_CTK_IDENT && token_pos)
            {
                if (arg_token.is_macro_param)
                {
                    assert(vector_count(lexer->macro_expansions) > 0);

                    layec_c_macro_expansion* current_macro_expansion = *vector_back(lexer->macro_expansions);
                    assert(current_macro_expansion);

                    for (long long mpai = 0; mpai < vector_count(current_macro_expansion->args[arg_token.macro_param_index]); mpai++)
                    {
                        layec_c_token mpai_token = current_macro_expansion->args[arg_token.macro_param_index][mpai];
                        if (mpai_token.kind == LAYEC_CTK_IDENT)
                        {
                            layec_c_macro_def* mpai_macro_def = layec_c_lexer_lookup_macro_def(lexer, mpai_token.string_value);
                            if (mpai_macro_def && !mpai_macro_def->has_params)
                            {
                                for (long long wpai = 0; wpai < vector_count(mpai_macro_def->body); wpai++)
                                    vector_push(current_arg, mpai_macro_def->body[wpai]);
                            }
                            else vector_push(current_arg, mpai_token);
                        }
                        else vector_push(current_arg, mpai_token);
                    }
                    continue;
                }
                else
                {
                    layec_c_macro_def* within_arg_macro_def = layec_c_lexer_lookup_macro_def(lexer, arg_token.string_value);
                    if (within_arg_macro_def && !within_arg_macro_def->has_params)
                    {
                        for (long long wamdi = 0; wamdi < vector_count(within_arg_macro_def->body); wamdi++)
                            vector_push(current_arg, within_arg_macro_def->body[wamdi]);
                        continue;
                    }
                }
            }

            vector_push(current_arg, arg_token);
        }
    }

    if (token_pos)
    {
        layec_c_macro_expansion* current_macro_expansion = *vector_back(lexer->macro_expansions);
        assert(current_macro_expansion);

        if (current_macro_expansion->arg_index != -1 &&
            current_macro_expansion->arg_position >= vector_count(current_macro_expansion->args[current_macro_expansion->arg_index]))
        {
            current_macro_expansion->arg_index = -1;
            current_macro_expansion->arg_position = 0;
        }
        
        if (current_macro_expansion->body_position >= vector_count(current_macro_expansion->def->body))
        {
            layec_c_macro_expansion_destroy(current_macro_expansion);
            vector_pop(lexer->macro_expansions);
        }
    }

    layec_c_macro_expansion* arg_macro_expansion = layec_c_macro_expansion_create(found_macro_def, args, -1);
    vector_push(lexer->macro_expansions, arg_macro_expansion);
    
    *out_token = (layec_c_token){0};
    layec_c_lexer_read_token(lexer, out_token);

    return true;
}

static void layec_c_lexer_read_token(layec_c_lexer* lexer, layec_c_token* out_token)
{
    assert(lexer);
    if (vector_count(lexer->macro_expansions) > 0)
    {
        layec_c_macro_expansion* macro_expansion = *vector_back(lexer->macro_expansions);
        assert(macro_expansion);

        if (macro_expansion->arg_index >= 0)
        {
            assert(macro_expansion->arg_index < vector_count(macro_expansion->args));
            if (macro_expansion->arg_position >= vector_count(macro_expansion->args[macro_expansion->arg_index]))
            {
                macro_expansion->arg_index = -1;
                macro_expansion->arg_position = 0;

                layec_c_lexer_read_token(lexer, out_token);
                return;
            }

            *out_token = macro_expansion->args[macro_expansion->arg_index][macro_expansion->arg_position];
            macro_expansion->arg_position++;

            if (out_token->kind == LAYEC_CTK_IDENT)
            {
                if (layec_c_lexer_pp_try_expand_token(lexer, *out_token,
                    &macro_expansion->arg_position,
                    &macro_expansion->args[macro_expansion->arg_index],
                    out_token))
                {
                    return;
                }
            }
        }
        else
        {
            assert(macro_expansion->def);
            vector(layec_c_token) body = macro_expansion->def->body;
            if (macro_expansion->body_position >= vector_count(body))
            {
                layec_c_macro_expansion_destroy(macro_expansion);
                vector_pop(lexer->macro_expansions);
                goto regular_lex_token;
            }

            assert(macro_expansion->body_position >= 0);
            assert(macro_expansion->def->body);
            if (macro_expansion->body_position >= vector_count(macro_expansion->def->body))
            {
                layec_c_macro_expansion_destroy(macro_expansion);
                vector_pop(lexer->macro_expansions);

                layec_c_lexer_read_token(lexer, out_token);
                return;
            }

            *out_token = macro_expansion->def->body[macro_expansion->body_position];
            macro_expansion->body_position++;

            if (out_token->kind == '#')
            {
                if (macro_expansion->body_position >= vector_count(macro_expansion->def->body))
                {
                    layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                        out_token->location);
                    printf("expected macro argument name");
                    layec_c_lexer_advance(lexer, true);
                    layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                        out_token->location);
                        
                    layec_c_macro_expansion_destroy(macro_expansion);
                    vector_pop(lexer->macro_expansions);
                    goto regular_lex_token;
                }

                *out_token = macro_expansion->def->body[macro_expansion->body_position];
                macro_expansion->body_position++;

                if (out_token->kind != LAYEC_CTK_IDENT)
                {
                    layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                        out_token->location);
                    printf("expected macro argument name");
                    layec_c_lexer_advance(lexer, true);
                    layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                        out_token->location);
                }
                else
                {
                    if (!out_token->is_macro_param)
                    {
                        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                            out_token->location);
                        printf("expected macro argument name");
                        layec_c_lexer_advance(lexer, true);
                        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                            out_token->location);
                    }
                    else
                    {
                        assert(out_token->macro_param_index < vector_count(macro_expansion->args));
                        vector(layec_c_token) arg_tokens = macro_expansion->args[out_token->macro_param_index];

                        layec_string_builder builder = {0};

                        for (long long ati = 0; ati < vector_count(arg_tokens); ati++)
                        {
                            if (ati > 0) layec_string_builder_append_rune(&builder, ' ');
                            layec_string_builder_append_string_view(&builder,
                                layec_location_get_source_image(lexer->context, arg_tokens[ati].location));
                        }

                        *out_token = (layec_c_token)
                        {
                            .kind = LAYEC_CTK_LIT_STRING,
                            .location = out_token->location,
                            .string_value = layec_context_intern_string_builder(lexer->context, builder),
                        };

                        layec_string_builder_destroy(&builder);
                    }
                }
            }
            else if (out_token->kind == LAYEC_CTK_IDENT)
            {
                if (out_token->is_macro_param)
                {
                    macro_expansion->arg_index = out_token->macro_param_index;
                    macro_expansion->arg_position = 0;

                    *out_token = (layec_c_token){0};
                    layec_c_lexer_read_token(lexer, out_token);
                    return;
                }
                else
                {
                    if (layec_c_lexer_pp_try_expand_token(lexer, *out_token,
                        &macro_expansion->body_position,
                        &macro_expansion->def->body,
                        out_token))
                    {
                        return;
                    }
                }
            }

            if (macro_expansion->body_position >= vector_count(macro_expansion->def->body))
            {
                layec_c_macro_expansion_destroy(macro_expansion);
                vector_pop(lexer->macro_expansions);
            }
        }

        return;
    }

regular_lex_token:;
    layec_c_lexer_eat_white_space(lexer);
    while (lexer->at_start_of_line && lexer->current_char == '#')
    {
        layec_c_lexer_handle_preprocessor_directive(lexer);
        assert(!lexer->is_in_preprocessor);
        layec_c_lexer_eat_white_space(lexer);
    }
    
    layec_c_lexer_read_token_no_preprocess(lexer, out_token);

    if (out_token->kind == LAYEC_CTK_IDENT)
    {
        if (layec_c_lexer_pp_try_expand_token(lexer, *out_token, NULL, NULL, out_token))
            return;
        
    not_a_macro:;
        for (int i = 0; c89_keywords[i].name != NULL; i++)
        {
            if (0 == strncmp(c89_keywords[i].name, out_token->string_value.data, (unsigned long long)out_token->string_value.length))
            {
                out_token->kind = c89_keywords[i].kind;
                break;
            }
        }
    }
}

static void layec_c_lexer_skip_to_end_of_directive(layec_c_lexer* lexer, layec_c_token token)
{
    assert(lexer);
    assert(lexer->is_in_preprocessor);
    while (token.kind != LAYEC_CTK_EOF && token.kind != '\n')
    {
        token = (layec_c_token){0};
        layec_c_lexer_read_token_no_preprocess(lexer, &token);
    }

    if (lexer->current_char != 0 && lexer->cur < lexer->end) assert(lexer->at_start_of_line);
    lexer->is_in_preprocessor = false;
}

#define LEXER_PAST_EOF(L) ((L)->cur >= (L)->end)

static void layec_c_lexer_handle_define_directive(layec_c_lexer* lexer, layec_c_token token)
{
    assert(lexer);
    assert(lexer->is_in_preprocessor);
    assert(token.kind == LAYEC_CTK_IDENT &&
        0 == strncmp(token.string_value.data, "define", (unsigned long long)token.string_value.length));

    layec_location start_location = token.location;
    layec_c_lexer_read_token_no_preprocess(lexer, &token);

    if (token.kind == LAYEC_CTK_EOF)
    {
        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
            token.location);
        printf("macro name missing");
        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
            token.location);

        return;
    }
    
    if (token.kind != LAYEC_CTK_IDENT)
    {
        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
            token.location);
        printf("macro name must be an identifier");
        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
            token.location);

        layec_c_lexer_skip_to_end_of_directive(lexer, token);
        return;
    }

    assert(token.kind == LAYEC_CTK_IDENT);
    layec_string_view macro_name = token.string_value;

    bool macro_has_params = false;
    vector(layec_string_view) macro_params = NULL;
    vector(layec_c_token) macro_body = NULL;

    if (LEXER_PAST_EOF(lexer))
        goto store_macro_in_translation_unit;
    assert(lexer->cur < lexer->end);

    if (lexer->current_char == '(' &&
        (lexer->current_char_location - lexer->source_buffer.text.data) == token.location.offset + token.location.length)
    {
        macro_has_params = true;
        layec_c_lexer_read_token_no_preprocess(lexer, &token);

        for (;;)
        {
            layec_c_lexer_read_token_no_preprocess(lexer, &token);
            if (token.kind == LAYEC_CTK_INVALID)
            {
                layec_c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }

            if (token.kind == LAYEC_CTK_EOF || token.kind == '\n')
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    token.location);
                printf("expected ')' in macro parameter list");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    token.location);

                layec_c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }

            if (token.kind != LAYEC_CTK_IDENT)
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    token.location);
                printf("invalid token in macro parameter list (expected identifier)");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    token.location);

                layec_c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }
        
            assert(token.kind == LAYEC_CTK_IDENT);
            vector_push(macro_params, token.string_value);

            layec_c_lexer_read_token_no_preprocess(lexer, &token);
            if (token.kind == ')')
                break;
            else if (token.kind != ',')
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    token.location);
                printf("expected comma in macro parameter list");
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    token.location);

                layec_c_lexer_skip_to_end_of_directive(lexer, token);
                return;
            }
        }

        //layec_c_lexer_read_token_no_preprocess(lexer, &token);
        if (token.kind != ')')
        {
            layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                token.location);
            printf("expected ')' in macro parameter list");
            layec_c_lexer_advance(lexer, true);
            layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                token.location);

            layec_c_lexer_skip_to_end_of_directive(lexer, token);
            return;
        }
    }

    for (;;)
    {
        token = (layec_c_token){0};
        layec_c_lexer_read_token_no_preprocess(lexer, &token);
        if (token.kind == LAYEC_CTK_INVALID)
        {
            layec_c_lexer_skip_to_end_of_directive(lexer, token);
            return;
        }

        if (token.kind == LAYEC_CTK_EOF || token.kind == '\n')
            break;

        if (token.kind == LAYEC_CTK_IDENT && macro_has_params)
        {
            for (long long i = 0; i < vector_count(macro_params); i++)
            {
                if (token.string_value.length != macro_params[i].length)
                    continue;

                if (0 == strncmp(token.string_value.data, macro_params[i].data, (unsigned long long)token.string_value.length))
                {
                    token.is_macro_param = true;
                    token.macro_param_index = i;
                    break;
                }
            }
        }

        vector_push(macro_body, token);
    }

store_macro_in_translation_unit:;
    layec_c_macro_def* def = calloc(1, sizeof *def);
    def->name = macro_name;
    def->has_params = macro_has_params;
    def->params = macro_params;
    def->body = macro_body;

    vector_push(lexer->tu->macro_defs, def);

    lexer->is_in_preprocessor = false;
    lexer->is_in_include = false;
}

static void layec_c_lexer_handle_include_directive(layec_c_lexer* lexer, layec_c_token token)
{
    assert(lexer);
    assert(lexer->is_in_preprocessor);
    assert(token.kind == LAYEC_CTK_IDENT &&
        0 == strncmp(token.string_value.data, "include", (unsigned long long)token.string_value.length));

    layec_location include_location = token.location;
    layec_c_lexer_read_token_no_preprocess(lexer, &token);
    lexer->is_in_include = true;

    bool is_angle_string = false;
    layec_string_view include_path = LAYEC_STRING_VIEW_EMPTY;

    if (token.kind == '\n')
    {
        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
            token.location);
        printf("expected a file name");
        layec_c_lexer_advance(lexer, true);
        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
            token.location);
            
        layec_c_lexer_skip_to_end_of_directive(lexer, token);
        return;
    }
    
    if (token.kind == LAYEC_CTK_LIT_STRING)
    {
        is_angle_string = token.is_angle_string;
        include_path = token.string_value;
    }
    else
    {
        // TODO(local): computed #include
        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
            token.location);
        printf("expected a file name");
        layec_c_lexer_advance(lexer, true);
        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
            token.location);
    }

    layec_c_lexer_skip_to_end_of_directive(lexer, token);
    
    lexer->is_in_preprocessor = false;
    lexer->is_in_include = false;

    // TODO(local): process the include
    int include_source_id = layec_context_get_or_add_source_buffer_from_file(lexer->context, include_path);
    if (include_source_id <= 0)
    {
        // TODO(local): only do this for quote strings, not angle strings
        if (!is_angle_string)
        {
            layec_string_view parent_dir = lexer->source_buffer.name;
            layec_string_view include_path2 = layec_string_view_path_concat(lexer->context, parent_dir, include_path);

            include_source_id = layec_context_get_or_add_source_buffer_from_file(lexer->context, include_path2);
            if (include_source_id > 0)
                goto good_include_gogogo;
        }

        for (long long i = 0; i < vector_count(lexer->context->include_dirs); i++)
        {
            layec_string_view include_dir = lexer->context->include_dirs[i];
            layec_string_view include_path2 = layec_string_view_path_concat(lexer->context, include_dir, include_path);
            
            include_source_id = layec_context_get_or_add_source_buffer_from_file(lexer->context, include_path2);
            if (include_source_id > 0)
                goto good_include_gogogo;
        }

        goto handle_include_error;
    }

good_include_gogogo:;
    layec_c_token_buffer include_token_buffer = layec_c_get_tokens(lexer->context, lexer->tu, include_source_id);
    
    layec_c_macro_def* include_macro_def = calloc(1, sizeof *include_macro_def);
    include_macro_def->name = LAYEC_STRING_VIEW_CONSTANT("< include >");
    include_macro_def->body = include_token_buffer.semantic_tokens;

    vector_push(lexer->tu->macro_defs, include_macro_def);
    layec_c_macro_def* include_macro_def_ptr = *vector_back(lexer->tu->macro_defs);

    layec_c_macro_expansion* macro_expansion = layec_c_macro_expansion_create(include_macro_def_ptr, NULL, -1);
    vector_push(lexer->macro_expansions, macro_expansion);
    return;

handle_include_error:;
    layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
        include_location);
    printf("could not read included file '%.*s'", LAYEC_STRING_VIEW_EXPAND(include_path));
    layec_c_lexer_advance(lexer, true);
    layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
        include_location);
}

static void layec_c_lexer_handle_preprocessor_directive(layec_c_lexer* lexer)
{
    assert(lexer);
    assert(!lexer->is_in_preprocessor);
    assert(lexer->at_start_of_line && lexer->current_char == '#');

    lexer->is_in_preprocessor = true;
    layec_c_lexer_advance(lexer, true);

    layec_location start_location = layec_c_lexer_get_location(lexer);

    layec_c_token token = {0};
    layec_c_lexer_read_token_no_preprocess(lexer, &token);

    switch (token.kind)
    {
        default:
        {
        invalid_preprocessing_directive:;
            layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                token.location);
            printf("invalid preprocessing directive");
            layec_c_lexer_advance(lexer, true);
            layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                token.location);

            layec_c_lexer_skip_to_end_of_directive(lexer, token);
            return;
        }

        case LAYEC_CTK_IDENT:
        {
            if (0 == strncmp(token.string_value.data, "define", (unsigned long long)token.string_value.length))
                layec_c_lexer_handle_define_directive(lexer, token);
            else if (0 == strncmp(token.string_value.data, "include", (unsigned long long)token.string_value.length))
                layec_c_lexer_handle_include_directive(lexer, token);
            else goto invalid_preprocessing_directive;
        } break;
    }
    
    assert(!lexer->is_in_preprocessor);
}

static bool layec_c_lexer_skip_backslash_newline(layec_c_lexer* lexer)
{
    if (!layec_c_lexer_at_eof(lexer) && *lexer->cur == '\\' &&
        (layec_c_lexer_peek_no_process(lexer, 1) == '\n' || (layec_c_lexer_peek_no_process(lexer, 1) == '\r' && layec_c_lexer_peek_no_process(lexer, 2) == '\n')))
    {
        lexer->cur++;
        assert(!layec_c_lexer_at_eof(lexer));

        if (*lexer->cur == '\n')
        {
            lexer->cur++;
            if (!layec_c_lexer_at_eof(lexer) && *lexer->cur == '\r')
                lexer->cur++;
        }
        else
        {
            assert(*lexer->cur == '\r' && layec_c_lexer_peek_no_process(lexer, 1) == '\n');
            lexer->cur += 2;
        }

        return true;
    }

    return false;
}

static int layec_c_lexer_read_next_char(layec_c_lexer* lexer, bool allow_comments)
{
    if (LEXER_PAST_EOF(lexer)) return 0;

    if (lexer->current_char == '\n')
        lexer->at_start_of_line = true;
    else if (!is_space(lexer->current_char) && lexer->current_char != 0)
        lexer->at_start_of_line = false;

    while (layec_c_lexer_skip_backslash_newline(lexer)) { }
    if (LEXER_PAST_EOF(lexer)) return 0;

    if (allow_comments && *lexer->cur == '/')
    {
        if (layec_c_lexer_peek_no_process(lexer, 2) == '/')
        {
            bool has_warned = false;

            lexer->cur += 2;
            while (!LEXER_PAST_EOF(lexer))
            {
                if (*lexer->cur == '\\' && layec_c_lexer_skip_backslash_newline(lexer))
                {
                    if (!has_warned)
                    {
                        // TODO(local): we could track the total length of the comment and report it after
                        layec_location location = (layec_location)
                        {
                            .source_id = lexer->source_id,
                            .offset = lexer->cur - lexer->source_buffer.text.data,
                            .length = 1,
                        };
                        layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_WARN,
                            location);
                        printf("Multiline // comment");
                        layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_WARN,
                            location);
                    }

                    has_warned = true;
                    continue;
                }

                if (*lexer->cur == '\n')
                    break;
                lexer->cur++;
            }

            return ' ';
        }
        else if (layec_c_lexer_peek_no_process(lexer, 2) == '*')
        {
            lexer->cur += 2;

            int lastc = 0;
            for (;;)
            {
                if (!LEXER_PAST_EOF(lexer) && *lexer->cur == '\\')
                    layec_c_lexer_skip_backslash_newline(lexer);

                if (LEXER_PAST_EOF(lexer))
                {
                    // TODO(local): we could track the total length of the comment and report it after
                    layec_location location = (layec_location)
                    {
                        .source_id = lexer->source_id,
                        .offset = lexer->cur - lexer->source_buffer.text.data,
                        .length = 1,
                    };
                    layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_WARN,
                        location);
                    printf("Unfinished /* comment");
                    layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_WARN,
                        location);
                    break;
                }

                char curc = *lexer->cur;
                lexer->cur++;

                // NOTE(local): is this standard compliant behavior for start of line + preprocessor directive?
                if (curc == '\n')
                    lexer->at_start_of_line = true;
                
                if (curc == '/' && lastc == '*')
                    break;
                
                lastc = curc;
            }

            return ' ';
        }
    }

    assert(!LEXER_PAST_EOF(lexer));
    int result = *lexer->cur;
    lexer->cur++;
    return result;
}

#undef LEXER_PAST_EOF

static int layec_c_lexer_peek_no_process(layec_c_lexer* lexer, int ahead)
{
    assert(ahead >= 1);
    const char* at = lexer->cur + ahead - 1;
    if (at >= lexer->end) return 0;
    return *at;
}

static bool layec_c_lexer_at_eof(layec_c_lexer* lexer)
{
    return lexer->current_char == 0;
}

static void layec_c_lexer_advance(layec_c_lexer* lexer, bool allow_comments)
{
    lexer->current_char_location = lexer->cur;
    lexer->current_char = layec_c_lexer_read_next_char(lexer, allow_comments);
    if (lexer->current_char != 0) assert(lexer->current_char_location < lexer->cur);
}
