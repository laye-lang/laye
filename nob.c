#if defined(_WIN32)
#    define NOB_WINDOWS
#    define NOB_PATH_SEP "\\"
#elif defined(__unix__)
#    define NOB_UNIX
#    define NOB_PATH_SEP "/"
#else
#    error "This build script currently only supports Windows and Unix, sorry"
#endif

#ifndef CC
#    if defined(__clang__)
#        define CC "clang"
#    elif defined(__GNUC__)
#        define CC "gcc"
#    elif defined(_MSC_VER)
#        define NOB_CL_EXE
#        define CC "cl.exe"
#    else
#        define CC "cc"
#    endif
#endif

#ifdef NOB_CL_EXE
#    define NOB_REBUILD_URSELF(binary_path, source_path) CC, source_path
#else
#    define NOB_REBUILD_URSELF(binary_path, source_path) CC, "-o", binary_path, source_path
#endif

#define NOB_IMPLEMENTATION
#include "nob.h"

typedef struct project_info {
    const char* name;
    const char* desc;
    const char* outfile;

    const char** includes;
    const char** sources;
} project_info;

// == Laye Compiler Driver ==
static const char* laye_driver_project_includes[] = {
    "./lca/include",
    "./lyir/include",
    "./ccly/include",
    "./laye/include",

    NULL, // sentinel
};

static const char* laye_driver_project_sources[] = {
    "./lyir/lib/irpass/abi.c",
    "./lyir/lib/irpass/validate.c",
    "./lyir/lib/cback.c",
    "./lyir/lib/context.c",
    "./lyir/lib/depgraph.c",
    "./lyir/lib/ir.c",
    "./lyir/lib/llvm.c",
    "./lyir/lib/shared.c",

    "./ccly/lib/context.c",
    "./ccly/lib/data.c",
    "./ccly/lib/debug.c",
    "./ccly/lib/parser.c",

    "./laye/lib/context.c",
    "./laye/lib/data.c",
    "./laye/lib/debug.c",
    "./laye/lib/irgen.c",
    "./laye/lib/parser.c",
    "./laye/lib/sema.c",

    "./laye/src/compiler.c",

    NULL, // sentinel
};

static project_info laye_driver_project = {
    "laye",
    "Laye Compiler Driver",
    "layec",

    laye_driver_project_includes,
    laye_driver_project_sources,
};
// == Laye Compiler Driver ==

static void path_get_file_name(const char** path_ref, int* length, bool remove_extension) {
    const char* path = *path_ref;
    *length = (int)strlen(path);

    if (remove_extension) {
        for (int i = *length; i >= 0; i--) {
            if (path[i] == '.') {
                *length = i;
                break;
            }
        }
    }

    for (int i = *length; i >= 0; i--) {
        if (path[i] == '/' || path[i] == '\\') {
            int index = i + 1;
            *length -= index;
            *path_ref += index;
            break;
        }
    }
}

static void path_get_first_directory_name(const char** path_ref, int* length) {
    const char* path = *path_ref;
    *length = (int)strlen(path);

    if (*length >= 0 && (0 == strncmp("./", path, 2) || 0 == strncmp(".\\", path, 2))) {
        *path_ref += 2;
        *length -= 2;
    }

    path = *path_ref;

    for (int i = 0, max_length = *length; i < max_length; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            *length = i;
            break;
        }
    }
}

static void cflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-std=c2x");
    nob_cmd_append(cmd, "-pedantic");
    nob_cmd_append(cmd, "-pedantic-errors");
    nob_cmd_append(cmd, "-ggdb");
    nob_cmd_append(cmd, "-Werror=return-type");
    nob_cmd_append(cmd, "-D__USE_POSIX");
    nob_cmd_append(cmd, "-D_XOPEN_SOURCE=600");
}

static void ldflags(Nob_Cmd* cmd) {
    nob_cmd_append(cmd, "-fsanitize=address");
}

static bool rebuild_c(project_info project, const char* source_file, const char* object_file) {
    bool result = true;
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, CC);
    cflags(&cmd);
    ldflags(&cmd);
    nob_cmd_append(&cmd, "-I", ".");
    for (int i = 0; project.includes[i] != NULL && i < 10; i++) {
        nob_cmd_append(&cmd, "-I", project.includes[i]);
    }
    nob_cmd_append(&cmd, "-o", object_file);
    nob_cmd_append(&cmd, "-c", source_file);

    nob_return_defer(nob_cmd_run_sync(cmd));

defer:;
    nob_cmd_free(cmd);
    return result;
}

static bool build_project(project_info project) {
    bool result = true;

    size_t checkpoint = nob_temp_save();
    Nob_File_Paths dependencies = {0};
    Nob_Cmd cmd = {0};

    const char* output_path = nob_temp_sprintf("./out/%s", project.outfile);

    if (!nob_mkdir_if_not_exists("./out")) nob_return_defer(false);
    if (!nob_mkdir_if_not_exists("./out/o")) nob_return_defer(false);

    if (!nob_mkdir_if_not_exists(nob_temp_sprintf("./out/o/%s", project.name))) {
        nob_return_defer(false);
    }

    for (int64_t i = 0; project.includes[i] != NULL && i < 10; i++) {
        nob_read_entire_dir_recursively(project.includes[i], &dependencies);
    }

    for (int64_t i = 0; project.sources[i] != NULL && i < 100; i++) {
        nob_da_append(&dependencies, project.sources[i]);
    }

    if (!nob_needs_rebuild(output_path, dependencies.items, dependencies.count)) {
        nob_return_defer(true);
    }

    nob_cmd_append(&cmd, CC);
    ldflags(&cmd);
    nob_cmd_append(&cmd, "-o", output_path);

    for (int64_t i = 0; project.sources[i] != NULL && i < 100; i++) {
        const char* source_file = project.sources[i];

        const char* file_name = source_file;
        int file_name_length = 0;
        path_get_file_name(&file_name, &file_name_length, true);

        const char* project_root_name = source_file;
        int project_root_name_length = 0;
        path_get_first_directory_name(&project_root_name, &project_root_name_length);

        const char* object_file = nob_temp_sprintf("./out/o/%s/%.*s_%.*s.o", project.name, project_root_name_length, project_root_name, file_name_length, file_name);
        nob_cmd_append(&cmd, object_file);

        if (nob_needs_rebuild1(object_file, source_file)) {
            rebuild_c(project, source_file, object_file);
        }
    }

    nob_return_defer(nob_cmd_run_sync(cmd));
    
defer:;
    nob_cmd_free(cmd);
    nob_da_free(dependencies);
    nob_temp_rewind(checkpoint);
    return result;
}

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    int result = 0;

    const char* program = nob_shift_args(&argc, &argv);

    if (!build_project(laye_driver_project)) {
        return 1;
    }

defer:
    return 0;
}
