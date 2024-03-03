#ifndef LCAPLAT_H
#define LCAPLAT_H

#include <stdbool.h>

bool lca_plat_stdout_isatty(void);
bool lca_plat_stderr_isatty(void);

bool lca_plat_file_exists(const char* file_path);

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

#endif // LCA_PLAT_IMPLEMENTATION

#endif // !LCAPLAT_H
