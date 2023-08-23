#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "layec/c/lexer.h"

typedef struct layec_c_lexer layec_c_lexer;
typedef struct keyword_info keyword_info;

struct layec_c_lexer
{
    layec_context* context;
    int source_id;
    layec_source_buffer source_buffer;

    const char* cur;
    const char* end;

    const char* current_char_location;
    int current_char;
    bool at_start_of_line;
};

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
        .offset = lexer->current_char_location - lexer->source_buffer.text,
        .length = 1,
    };
}

static bool is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
static bool is_digit(int c) { return c >= '0' && c <= '9'; }
static bool is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static bool is_alpha_numeric(int c) { return is_digit(c) || is_alpha(c); }
static bool is_hex_digit(int c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

static int get_digit_value(int c)
{
    if (is_digit(c)) return c - '0';
    else if (c >= 'a' && c <= 'z') return c - 'a' + 11;
    else if (c >= 'A' && c <= 'Z') return c - 'A' + 11;
    return 0;
}

static bool layec_c_lexer_at_eof(layec_c_lexer* lexer);
static void layec_c_lexer_advance(layec_c_lexer* lexer, bool allow_comments);
static int layec_c_lexer_peek_no_process(layec_c_lexer* lexer, int ahead);
static void layec_c_lexer_read_token(layec_c_lexer* lexer, layec_c_token* out_token);

layec_c_token_buffer layec_c_get_tokens(layec_context* context, int source_id)
{
    assert(context);

    layec_c_lexer lexer = {
        .context = context,
        .source_id = source_id,
        .at_start_of_line = true,
    };

    lexer.source_buffer = layec_context_get_source_buffer(context, source_id);
    assert(lexer.source_buffer.text);

    lexer.cur = lexer.source_buffer.text;
    lexer.end = lexer.cur + strlen(lexer.source_buffer.text);

    layec_c_lexer_advance(&lexer, true);

    layec_c_token_buffer token_buffer = {0};
    while (!layec_c_lexer_at_eof(&lexer))
    {
        layec_c_token token = {0};
        layec_c_lexer_read_token(&lexer, &token);
        if (token.kind == LAYEC_CTK_EOF) break;
        vector_push(token_buffer.tokens, token);
    }

    return token_buffer;
}

static void layec_c_lexer_eat_white_space(layec_c_lexer* lexer)
{
    while (is_space(lexer->current_char)) layec_c_lexer_advance(lexer, true);
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

    layec_location start_location = layec_c_lexer_get_location(lexer);
    out_token->location = start_location;

    int cur = lexer->current_char;
    switch (cur)
    {
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
                else layec_c_lexer_advance(lexer, false);
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
            // TODO(local): When we write (or copy an old) UTF-8 encoder/decoder, do string building as well
            vector(char) string_data = NULL;

            layec_c_lexer_advance(lexer, false);
            out_token->kind = LAYEC_CTK_LIT_STRING;

            while (lexer->current_char != '"')
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
                else if (lexer->current_char == '\\')
                    layec_c_lexer_read_escape_sequence(lexer, false);
                else layec_c_lexer_advance(lexer, false);
            }

            if (lexer->current_char != '"')
            {
                layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
                printf("missing closing quote");
                layec_c_lexer_advance(lexer, true);
                layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                    start_location);
            }
            else layec_c_lexer_advance(lexer, false);

            vector_push(string_data, 0);
            out_token->string_value = layec_string_view_create(string_data, vector_count(string_data));
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
                out_token->string_value = layec_string_view_create(lexer->source_buffer.text + suffix_location.offset,
                    suffix_end_location.offset - suffix_location.offset);
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
            out_token->string_value = layec_string_view_create(lexer->source_buffer.text + start_location.offset,
                ident_end_location.offset - start_location.offset);

            for (int i = 0; c89_keywords[i].name != NULL; i++)
            {
                if (0 == strncmp(c89_keywords[i].name, out_token->string_value.data, (unsigned long long)out_token->string_value.length))
                {
                    out_token->kind = c89_keywords[i].kind;
                    break;
                }
            }
        } break;

        default:
        {
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

static void layec_c_lexer_read_token(layec_c_lexer* lexer, layec_c_token* out_token)
{
    layec_c_lexer_read_token_no_preprocess(lexer, out_token);
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
    if (layec_c_lexer_at_eof(lexer)) return 0;

    if (lexer->current_char == '\n')
        lexer->at_start_of_line = true;
    else if (!is_space(lexer->current_char))
        lexer->at_start_of_line = false;

    while (layec_c_lexer_skip_backslash_newline(lexer)) { }
    if (layec_c_lexer_at_eof(lexer)) return 0;

    if (allow_comments && *lexer->cur == '/')
    {
        if (layec_c_lexer_peek_no_process(lexer, 2) == '/')
        {
            bool has_warned = false;

            lexer->cur += 2;
            while (!layec_c_lexer_at_eof(lexer))
            {
                if (*lexer->cur == '\\' && layec_c_lexer_skip_backslash_newline(lexer))
                {
                    if (!has_warned)
                    {
                        // TODO(local): we could track the total length of the comment and report it after
                        layec_location location = (layec_location)
                        {
                            .source_id = lexer->source_id,
                            .offset = lexer->cur - lexer->source_buffer.text,
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
                if (!layec_c_lexer_at_eof(lexer) && *lexer->cur == '\\')
                    layec_c_lexer_skip_backslash_newline(lexer);

                if (layec_c_lexer_at_eof(lexer))
                {
                    // TODO(local): we could track the total length of the comment and report it after
                    layec_location location = (layec_location)
                    {
                        .source_id = lexer->source_id,
                        .offset = lexer->cur - lexer->source_buffer.text,
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

    assert(!layec_c_lexer_at_eof(lexer));
    int result = *lexer->cur;
    lexer->cur++;
    return result;
}

static int layec_c_lexer_peek_no_process(layec_c_lexer* lexer, int ahead)
{
    assert(ahead >= 1);
    const char* at = lexer->cur + ahead - 1;
    if (at >= lexer->end) return 0;
    return *at;
}

static bool layec_c_lexer_at_eof(layec_c_lexer* lexer)
{
    return lexer->cur >= lexer->end;
}

static void layec_c_lexer_advance(layec_c_lexer* lexer, bool allow_comments)
{
    lexer->current_char_location = lexer->cur;
    lexer->current_char = layec_c_lexer_read_next_char(lexer, allow_comments);
    assert(lexer->current_char_location < lexer->cur);
}
