#ifndef LAYEC_ASSERT_H
#define LAYEC_ASSERT_H

#ifdef NDEBUG
#  define assert(condition) ((void)0)
#else
#  define assert(condition) /* TODO(local): implement assert */
#endif

#endif // LAYEC_ASSERT_H
