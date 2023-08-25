#ifndef LAYEC_LIBC_STDLIB_H
#define LAYEC_LIBC_STDLIB_H

void exit(int status_code);

void* malloc(unsigned long long count);
void* calloc(unsigned long long count, unsigned long long size);
void* realloc(void* memory, unsigned long long count);
void free(void* memory);

#endif // LAYEC_LIBC_STDLIB_H
