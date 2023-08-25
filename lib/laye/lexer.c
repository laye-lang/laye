#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "layec/laye/token.h"
#include "layec/string.h"
#include "layec/laye/lexer.h"

typedef struct layec_laye_lexer layec_laye_lexer;

struct layec_laye_lexer
{
    layec_context* context;
    int source_id;
    layec_source_buffer source_buffer;

    const char* cur;
    const char* end;

    const char* current_char_location;
    int current_char;
};

struct keyword_info
{
    const char* name;
    layec_laye_token_kind kind;
};

struct keyword_info laye1_keywords[] = {
    {"true", LAYEC_LTK_TRUE},
    {"false", LAYEC_LTK_FALSE},
    {"nil", LAYEC_LTK_NIL},
    {"global", LAYEC_LTK_GLOBAL},
    {"if", LAYEC_LTK_IF},
    {"else", LAYEC_LTK_ElSE},
    {"for", LAYEC_LTK_FOR},
    {"do", LAYEC_LTK_DO},
    {"switch", LAYEC_LTK_SWITCH},
    {"case", LAYEC_LTK_CASE},
    {"default", LAYEC_LTK_DEFAULT},
    {"return", LAYEC_LTK_RETURN},
    {"break", LAYEC_LTK_BREAK},
    {"continue", LAYEC_LTK_CONTINUE},
    {"defer", LAYEC_LTK_DEFER},
    {"goto", LAYEC_LTK_GOTO},
    {"import", LAYEC_LTK_IMPORT},
    {"struct", LAYEC_LTK_STRUCT},
    {"variant", LAYEC_LTK_VARIANT},
    {"enum", LAYEC_LTK_ENUM},
    {"test", LAYEC_LTK_TEST},
    {"new", LAYEC_LTK_NEW},
    {"delete", LAYEC_LTK_DELETE},
    {"cast", LAYEC_LTK_CAST},
    {"try", LAYEC_LTK_TRY},
    {"catch", LAYEC_LTK_CATCH},
    {"discard", LAYEC_LTK_DISCARD},
    {"sizeof", LAYEC_LTK_SIZEOF},
    {"alignof", LAYEC_LTK_ALIGNOF},
    {"offsetof", LAYEC_LTK_OFFSETOF},
    {"not", LAYEC_LTK_NOT},
    {"and", LAYEC_LTK_AND},
    {"or", LAYEC_LTK_OR},
    {"xor", LAYEC_LTK_XOR},
    {"varargs", LAYEC_LTK_VARARGS},
    {"const", LAYEC_LTK_CONST},
    {"export", LAYEC_LTK_EXPORT},
    {"inline", LAYEC_LTK_INLINE},
    {"foreign", LAYEC_LTK_FOREIGN},
    {"callconv", LAYEC_LTK_CALLCONV},
    {"readonly", LAYEC_LTK_READONLY},
    {"writeonly", LAYEC_LTK_WRITEONLY},
    {"var", LAYEC_LTK_VAR},
    {"void", LAYEC_LTK_VOID},
    {"noreturn", LAYEC_LTK_NORETURN},
    {"rawptr", LAYEC_LTK_RAWPTR},
    {"bool", LAYEC_LTK_BOOL},
    {"int", LAYEC_LTK_INT},
    {"uint", LAYEC_LTK_UINT},
    {"float", LAYEC_LTK_FLOAT},
    {0},
};

static layec_location layec_laye_lexer_get_location(layec_laye_lexer *lexer)
{
    return (layec_location)
    {
        .source_id = lexer->source_id,
        .offset = lexer->current_char_location - lexer->source_buffer.text,
        .length = 1,
    };
}

static bool layec_laye_lexer_at_eof(layec_laye_lexer* lexer);
static void layec_laye_lexer_advance(layec_laye_lexer* lexer);
static int layec_laye_lexer_peek(layec_laye_lexer* lexer, int ahead);
static void layec_laye_lexer_read_token(layec_laye_lexer* lexer, layec_laye_token* out_token);

layec_laye_token_buffer layec_laye_get_tokens(layec_context* context, int source_id)
{
    assert(context);

    layec_laye_lexer lexer =
    {
        .context = context,
        .source_id = source_id,
    };

    lexer.source_buffer = layec_context_get_source_buffer(context, source_id);
    assert(lexer.source_buffer.text);

    lexer.cur = lexer.source_buffer.text;
    lexer.end = lexer.cur + strlen(lexer.source_buffer.text);

    layec_laye_lexer_advance(&lexer);

    layec_laye_token_buffer token_buffer = {0};
    for (;;)
    {
        layec_laye_token token = {0};
        const char* start_location = lexer.current_char_location;
        layec_laye_lexer_read_token(&lexer, &token);
        if (token.kind == LAYEC_LTK_EOF) break;
        assert(start_location != lexer.current_char_location);
        vector_push(token_buffer.tokens, token);
    }

    return token_buffer;
}

static void layec_laye_lexer_eat_white_space(layec_laye_lexer* lexer)
{
    while (!layec_laye_lexer_at_eof(lexer))
    {
        if (is_space(lexer->current_char))
            layec_laye_lexer_advance(lexer);
        else if (lexer->current_char == '/' && layec_laye_lexer_peek(lexer, 1) == '/')
        {
            while (!layec_laye_lexer_at_eof(lexer) && lexer->current_char != '\n')
                layec_laye_lexer_advance(lexer);
        }
        else if (lexer->current_char == '/' && layec_laye_lexer_peek(lexer, 1) == '*')
        {
            layec_location start_location = layec_laye_lexer_get_location(lexer);

            layec_laye_lexer_advance(lexer);
            layec_laye_lexer_advance(lexer);

            int stack = 1;
            int lastc = 0;
            while (stack > 0 && !layec_laye_lexer_at_eof(lexer))
            {
                int curc = lexer->current_char;
                layec_laye_lexer_advance(lexer);

                if (lastc == '*' && curc == '/')
                {
                    lastc = 0;
                    stack--;
                }
                else if (lastc == '/' && curc == '*')
                {
                    lastc = 0;
                    stack++;
                }
                else lastc = curc;
            }

            if (stack > 0)
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("unfinished /* comment");
                layec_laye_lexer_advance(lexer);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
        }
        else break;
    }
}

static void layec_laye_lexer_read_token(layec_laye_lexer* lexer, layec_laye_token* out_token)
{
    assert(lexer);

    layec_laye_lexer_eat_white_space(lexer);
    if (layec_laye_lexer_at_eof(lexer))
    {
        out_token->kind = LAYEC_LTK_EOF;
        return;
    }

    layec_location start_location = layec_laye_lexer_get_location(lexer);
    out_token->location = start_location;

    int cur = lexer->current_char;
    switch (cur)
    {
        case '(': case ')':
        case '[': case ']':
        case '{': case '}':
        case '.': case ',':
        case ';':
        {
            layec_laye_lexer_advance(lexer);
            out_token->kind = (layec_laye_token_kind)cur;
        } break;

        case ':':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == ':')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_COLON_COLON;
            }
            else out_token->kind = ':';
        } break;

        case '+':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_PLUS_EQUAL;
            }
            else out_token->kind = '+';
        } break;

        case '-':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_MINUS_EQUAL;
            }
            else out_token->kind = '-';
        } break;

        case '*':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_STAR_EQUAL;
            }
            else out_token->kind = '*';
        } break;

        case '/':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_SLASH_EQUAL;
            }
            else out_token->kind = '/';
        } break;

        case '%':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_PERCENT_EQUAL;
            }
            else out_token->kind = '%';
        } break;

        case '&':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_AMPERSAND_EQUAL;
            }
            else out_token->kind = '&';
        } break;

        case '|':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_PIPE_EQUAL;
            }
            else out_token->kind = '|';
        } break;

        case '~':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_TILDE_EQUAL;
            }
            else out_token->kind = '~';
        } break;

        case '<':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_LESS_EQUAL;
            }
            else if (lexer->current_char == '<')
            {
                layec_laye_lexer_advance(lexer);
                if (lexer->current_char == '=')
                {
                    layec_laye_lexer_advance(lexer);
                    out_token->kind = LAYEC_LTK_LESS_LESS_EQUAL;
                }
                else out_token->kind = LAYEC_LTK_LESS_LESS;
            }
            else out_token->kind = '<';
        } break;

        case '>':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_GREATER_EQUAL;
            }
            else if (lexer->current_char == '>')
            {
                layec_laye_lexer_advance(lexer);
                if (lexer->current_char == '=')
                {
                    layec_laye_lexer_advance(lexer);
                    out_token->kind = LAYEC_LTK_GREATER_GREATER_EQUAL;
                }
                else out_token->kind = LAYEC_LTK_GREATER_GREATER;
            }
            else out_token->kind = '>';
        } break;

        case '=':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_EQUAL_EQUAL;
            }
            else if (lexer->current_char == '>')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_EQUAL_GREATER;
            }
            else out_token->kind = '=';
        } break;

        case '!':
        {
            layec_laye_lexer_advance(lexer);
            if (lexer->current_char == '=')
            {
                layec_laye_lexer_advance(lexer);
                out_token->kind = LAYEC_LTK_BANG_EQUAL;
            }
            else out_token->kind = '!';
        } break;
        
        case '0': case '1': case '2': case '3': case '4': 
        case '5': case '6': case '7': case '8': case '9':
        {
            out_token->kind = LAYEC_LTK_LIT_INT;

            bool ends_with_underscore = false;

            long long integer_value = 0;
            bool is_int_too_large = false;

            while (is_digit(lexer->current_char) || lexer->current_char == '_')
            {
                // TODO(local): actually compute the integer value
                ends_with_underscore = lexer->current_char == '_';
                layec_laye_lexer_advance(lexer);
            }

            if (is_alpha(lexer->current_char))
                goto parse_ident;

            if (ends_with_underscore)
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("number literal cannot end in an underscore");
                layec_laye_lexer_advance(lexer);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
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
        parse_ident:;
            out_token->kind = LAYEC_LTK_IDENT;

            while (is_alpha_numeric(lexer->current_char) || lexer->current_char == '_')
                layec_laye_lexer_advance(lexer);

            layec_location ident_end_location = layec_laye_lexer_get_location(lexer);
            out_token->string_value = layec_string_view_create(lexer->source_buffer.text + start_location.offset,
                ident_end_location.offset - start_location.offset);
                
            for (int i = 0; laye1_keywords[i].name != NULL; i++)
            {
                if (0 == strncmp(laye1_keywords[i].name, out_token->string_value.data, (unsigned long long)out_token->string_value.length))
                {
                    out_token->kind = laye1_keywords[i].kind;
                    break;
                }
            }
        } break;

        default:
        {
            layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                start_location);
            printf("invalid character in source text");
            layec_laye_lexer_advance(lexer);
            layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                start_location);
        } break;
    }

finish_token:;
    layec_location end_location = layec_laye_lexer_get_location(lexer);
    out_token->location.length = end_location.offset - start_location.offset;
}

static bool layec_laye_lexer_at_eof(layec_laye_lexer* lexer)
{
    return lexer->current_char == 0;
}

static int layec_laye_lexer_peek(layec_laye_lexer* lexer, int ahead)
{
    assert(lexer);
    assert(ahead >= 1);

    const char* peek_at = lexer->cur + ahead - 1;
    if (peek_at >= lexer->end) return 0;

    assert(peek_at);
    return *peek_at;
}

static void layec_laye_lexer_advance(layec_laye_lexer* lexer)
{
    assert(lexer);

    if (lexer->cur >= lexer->end)
    {
        lexer->current_char = 0;
        return;
    }

    assert(lexer->cur);
    lexer->current_char_location = lexer->cur;
    lexer->current_char = *lexer->cur;

    lexer->cur++;
}
