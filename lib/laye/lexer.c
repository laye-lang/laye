#include <assert.h>
#include <stdbool.h>
#include <string.h>

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
        layec_laye_lexer_read_token(&lexer, &token);
        if (token.kind == LAYEC_LTK_EOF) break;
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
        // TODO(local): eat up comments, too
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
        // TODO(local): get some tokens
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
