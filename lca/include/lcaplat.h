/*
This software is available under 2 licenses -- choose whichever you prefer.
------------------------------------------------------------------------------
ALTERNATIVE A - MIT License
Copyright (c) 2023 Local Atticus
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
------------------------------------------------------------------------------
ALTERNATIVE B - Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef LCAPLAT_H
#define LCAPLAT_H

#include <stdbool.h>

bool lca_plat_stdout_isatty(void);
bool lca_plat_stderr_isatty(void);

bool lca_plat_file_exists(const char* file_path);

const char* lca_plat_self_exe(void);

#ifdef LCA_PLAT_IMPLEMENTATION

#include <stdio.h>

#ifdef _WIN32
#    define NOMINMAX
#    include <io.h>
#    include <Windows.h>
#    define isatty _isatty
#endif

#ifdef __linux__
#    include <execinfo.h>
#    include <unistd.h>
#    include <sys/stat.h>
#endif

#include <errno.h>

#include "lcamem.h"

bool lca_plat_stdout_isatty(void) {
    return isatty(fileno(stdout));
}

bool lca_plat_stderr_isatty(void) {
    return isatty(fileno(stderr));
}

bool lca_plat_file_exists(const char* file_path) {
#ifdef  __linux__
    struct stat stat_info = {0};

    int err = stat(file_path, &stat_info);
    if (err != 0) {
        if (errno == ENOENT) {
            return false;
        }

        perror("Failed to `stat` file");
        return false;
    }

    return true;
#else
    FILE* f = fopen(file_path, "r");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
#endif
}

const char* lca_plat_self_exe(void) {
#if defined(__linux__)
    char buffer[1024] = {0};
    ssize_t n = readlink("/proc/self/exe", buffer, 1024);
    if (n < 0) {
        return NULL;
    }
    return lca_temp_sprintf("%s", buffer);
#elif define(_WIN32)
    assert(false && "lca_plat_self_exe is not implemented on this platform");
    return NULL;
#else
    assert(false && "lca_plat_self_exe is not implemented on this platform");
    return NULL;
#endif
}

#endif // LCA_PLAT_IMPLEMENTATION

#endif // !LCAPLAT_H
