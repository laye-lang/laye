#ifndef LAYEC_LIBC_STDBOOL_H
#define LAYEC_LIBC_STDBOOL_H

#if __STDC_VERSION__ >= 199901L
#define bool _Bool
#define false 0
#define true 1
#define __bool_true_false_are_defined 1
#endif

#endif // LAYEC_LIBC_STDBOOL_H
