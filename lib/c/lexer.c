#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "layec/c/lexer.h"

typedef struct layec_c_lexer layec_c_lexer;

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

    layec_c_token_buffer tokens = {0};

    return tokens;
}

static void layec_c_lexer_read_token_no_preprocess(layec_c_lexer* lexer, layec_c_token* out_token)
{
    layec_location start_location = layec_c_lexer_get_location(lexer);
    switch (lexer->current_char)
    {
        default:
        {
            layec_context_issue_diagnostic_prolog(lexer->context, LAYEC_SEV_ERROR,
                start_location);
            printf("Invalid character in source text");
            layec_context_issue_diagnostic_epilog(lexer->context, LAYEC_SEV_ERROR,
                start_location);
        }
    }
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
        if (layec_c_lexer_peek_no_process(lexer, 1) == '/')
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
        else if (layec_c_lexer_peek_no_process(lexer, 1) == '*')
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
    const char* at = lexer->cur + ahead;
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
