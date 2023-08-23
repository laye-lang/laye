#ifndef LAYEC_ASSERT_H
#define LAYEC_ASSERT_H

#include <stdio.h>
#include <stdlib.h>

#ifdef NDEBUG
#  define assert(condition) ((void)0)
#else
#  define assert(condition) do { if (!(condition)) { printf(__FILE__":%d: ASSERT FAIL: "  #condition "\n", __LINE__); exit(1); } } while (0)
#endif

#endif // LAYEC_ASSERT_H
