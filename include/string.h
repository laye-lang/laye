#ifndef LAYEC_LIBC_STRING_H
#define LAYEC_LIBC_STRING_H

int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, unsigned long long n);
unsigned long long strlen(const char* s);
void* memcpy(void* dest, const void* source, unsigned long long count);
void* memset(void* dest, int value, unsigned long count);

#endif // LAYEC_LIBC_STRING_H
