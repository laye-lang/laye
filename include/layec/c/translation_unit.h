#ifndef LAYEC_C_TRANSLATION_UNIT_H
#define LAYEC_C_TRANSLATION_UNIT_H

#include "layec/string.h"
#include "layec/vector.h"
#include "layec/c/token.h"

typedef struct layec_c_macro_def layec_c_macro_def;
typedef struct layec_c_translation_unit layec_c_translation_unit;

struct layec_c_macro_def
{
    layec_string_view name;
    bool has_params;
    vector(layec_string_view) params;
    vector(layec_c_token) body;
};

struct layec_c_translation_unit
{
    vector(layec_c_macro_def*) macro_defs;
};

#endif // LAYEC_C_TRANSLATION_UNIT_H
