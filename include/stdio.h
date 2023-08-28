#ifndef LAYEC_LIBC_STDIO_H
#define LAYEC_LIBC_STDIO_H

typedef void FILE;

typedef unsigned long long size_t;

#define NULL      ((void*)0)
#define EOF       (-1)

#define SEEK_SET  (0)
#define SEEK_CUR  (1)
#define SEEK_END  (2)

int printf(const char* format, ...);

FILE* fopen(const char* file_name, const char* mode);
int fclose(FILE* stream);
size_t fread(void* buffer, size_t size, size_t member_count, FILE* stream);
int fseek(FILE* stream, long int offset, int whence);
long int ftell(FILE* stream);
int ferror(FILE* stream);

#endif // LAYEC_LIBC_STDIO_H
