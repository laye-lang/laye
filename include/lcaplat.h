#ifndef LCAPLAT_H
#define LCAPLAT_H

#include <stdbool.h>

bool lca_plat_stdout_isatty(void);
bool lca_plat_stderr_isatty(void);

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
#endif

bool lca_plat_stdout_isatty(void) {
    return isatty(fileno(stdout));
}

bool lca_plat_stderr_isatty(void) {
    return isatty(fileno(stderr));
}

#endif // LCA_PLAT_IMPLEMENTATION

#endif // !LCAPLAT_H
