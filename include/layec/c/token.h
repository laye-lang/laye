#ifndef LAYEC_C_TOKEN_H
#define LAYEC_C_TOKEN_H

#include "layec/source.h"

typedef struct layec_c_token layec_c_token;

struct layec_c_token
{
    int kind;
    layec_location location;

    char *string_value;
    long long string_length;

    long long int_value;
    
    double real_value;
};

#endif // LAYEC_C_TOKEN_H
