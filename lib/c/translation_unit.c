#include <stdio.h>

#include "layec/c/translation_unit.h"

void layec_c_macro_def_destroy(layec_c_macro_def* def)
{
    vector_free(def->params);
    vector_free(def->body);

    *def = (layec_c_macro_def){0};
    free(def);
}

void layec_c_translation_unit_destroy(layec_c_translation_unit* tu)
{
    for (long long i = 0; i < vector_count(tu->macro_defs); i++)
        layec_c_macro_def_destroy(tu->macro_defs[i]);

    vector_free(tu->macro_defs);
    
    *tu = (layec_c_translation_unit){0};
    free(tu);
}
