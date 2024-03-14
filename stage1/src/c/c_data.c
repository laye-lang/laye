#include "c.h"

#include <assert.h>

const char* c_token_kind_to_cstring(c_token_kind kind) {
    static bool single_chars_initialized = false;
    static char single_chars[256 * 2];

    switch (kind) {
        case C_TOKEN_INVALID: return "<invalid C token kind>";

#define X(N) \
    case C_TOKEN_##N: return #N;
            C_TOKEN_KINDS(X)
#undef X

        default: {
            if (kind < 256) {
                if (!single_chars_initialized) {
                    for (int i = 0; i < 256; i++)
                        single_chars[i * 2] = (char)i;
                }

                return &single_chars[kind * 2];
            }

            return "<unknown C token kind>";
        }
    }
}

void c_translation_unit_destroy(c_translation_unit* tu) {
    if (tu == NULL) return;

    assert(tu->context != NULL);
    lca_allocator allocator = tu->context->allocator;

    lca_arena_destroy(tu->arena);
    arr_free(tu->_all_tokens);

    *tu = (c_translation_unit){0};
    lca_deallocate(allocator, tu);
}
