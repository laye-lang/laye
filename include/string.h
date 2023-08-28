#ifndef LAYEC_LIBC_STRING_H
#define LAYEC_LIBC_STRING_H

int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, unsigned long long n);
unsigned long long strlen(const char* s);
void* memcpy(void* dest, const void* source, unsigned long long count);
#ifdef WIN32
void* memset(void* dest, int value, unsigned long long count);
#else
void* memset(void* dest, int value, unsigned long count);
#endif

#endif // LAYEC_LIBC_STRING_H
