#ifndef LAYEC_STDIO_H
#define LAYEC_STDIO_H

typedef void FILE;

typedef unsigned long long size_t;

#define NULL      ((void*)0)
#define EOF       (-1)

#define SEEK_SET  (0)
#define SEEK_CUR  (1)
#define SEEK_END  (2)

// NOTE(local): this kind of thing is going to be the hardest to port over without just copying parts from actual std library headers
#ifdef WIN32
FILE* __cdecl __acrt_iob_func(unsigned _Ix);
#  define stdin  (__acrt_iob_func(0))
#  define stdout (__acrt_iob_func(1))
#  define stderr (__acrt_iob_func(2))
#endif

int printf(const char* format, ...);

FILE* fopen(const char* file_name, const char* mode);
int fclose(FILE* stream);
size_t fread(void* buffer, size_t size, size_t member_count, FILE* stream);
int fseek(FILE* stream, long int offset, int whence);
long int ftell(FILE* stream);

#endif // LAYEC_STDIO_H
