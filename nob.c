#if defined(_WIN32)
#    define NOB_WINDOWS
#    define NOB_PATH_SEP "\\"
#    define EXE_SUFFIX   ".exe"
#    define _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS_GLOBALS
#    define _CRT_NONSTDC_NO_WARNINGS
#elif defined(__unix__)
#    define NOB_UNIX
#    define NOB_PATH_SEP "/"
#    define EXE_SUFFIX ""
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

// == Laye Legacy Compiler Driver ==
static const char* layec0_driver_project_includes[] = {
    "./lca/include",
    "./lyir/include",
    "./ccly/include",
    "./laye/include",

    NULL, // sentinel
};

static const char* layec0_driver_project_sources[] = {
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
    "./ccly/lib/lexer.c",
    "./ccly/lib/parser.c",

    "./laye/lib/context.c",
    "./laye/lib/data.c",
    "./laye/lib/debug.c",
    "./laye/lib/dependence.c",
    "./laye/lib/irgen.c",
    "./laye/lib/parser.c",
    "./laye/lib/sema.c",

    "./laye/src/compiler.c",

    NULL, // sentinel
};

static project_info layec0_driver_project = {
    "laye_legacy",
    "Laye Legacy Compiler Driver",
    "layec0" EXE_SUFFIX,

    layec0_driver_project_includes,
    layec0_driver_project_sources,
};
// == Laye Legacy Compiler Driver ==

// == CCLY Compiler Driver ==
static const char* ccly_driver_project_includes[] = {
    "./lca/include",
    "./lyir/include",
    "./ccly/include",

    NULL, // sentinel
};

static const char* ccly_driver_project_sources[] = {
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
    "./ccly/lib/driver.c",
    "./ccly/lib/lexer.c",
    "./ccly/lib/parser.c",

    "./ccly/src/ccly.c",

    NULL, // sentinel
};

static project_info ccly_driver_project = {
    "ccly",
    "CCLY Compiler Driver",
    "ccly" EXE_SUFFIX,

    ccly_driver_project_includes,
    ccly_driver_project_sources,
};
// == CCLY Compiler Driver ==

// == Laye Compiler Driver ==
static const char* laye_compiler_driver_includes[] = {
    "./lca/include",
    "./lyir/include",
    "./ccly/include",
    "./laye/include",

    NULL, // sentinel
};

static const char* laye_compiler_driver_sources[] = {
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
    "./ccly/lib/driver.c",
    "./ccly/lib/lexer.c",
    "./ccly/lib/parser.c",

    "./laye/lib/context.c",
    "./laye/lib/data.c",
    "./laye/lib/debug.c",
    "./laye/lib/dependence.c",
    "./laye/lib/driver.c",
    "./laye/lib/irgen.c",
    "./laye/lib/parser.c",
    "./laye/lib/sema.c",

    "./laye/src/laye.c",

    NULL, // sentinel
};

static project_info laye_compiler_driver = {
    "laye",
    "Laye Compiler Driver",
    "laye" EXE_SUFFIX,

    laye_compiler_driver_includes,
    laye_compiler_driver_sources,
};
// == Laye Compiler Driver ==

// == Laye Execution Test Runner ==
static const char* exec_test_runner_project_includes[] = {
    "./lca/include",
    NULL, // sentinel
};

static const char* exec_test_runner_project_sources[] = {
    "./laye/src/exec_test_runner.c",
    NULL, // sentinel
};

static project_info exec_test_runner_project = {
    "exec_test_runner",
    "Laye Execution Test Runner",
    "exec_test_runner" EXE_SUFFIX,

    exec_test_runner_project_includes,
    exec_test_runner_project_sources,
};
// == Laye Execution Test Runner ==

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

static void cflags(Nob_Cmd* cmd, bool debug) {
    nob_cmd_append(cmd, "-std=c2x");
    nob_cmd_append(cmd, "-pedantic");
    nob_cmd_append(cmd, "-pedantic-errors");
    nob_cmd_append(cmd, "-Wall");
    nob_cmd_append(cmd, "-Wextra");
    nob_cmd_append(cmd, "-Wno-unused-parameter");
    nob_cmd_append(cmd, "-Wno-unused-variable");
    nob_cmd_append(cmd, "-Wno-unused-function");
    nob_cmd_append(cmd, "-Wno-gnu-zero-variadic-macro-arguments");
    nob_cmd_append(cmd, "-Wno-missing-field-initializers");
    nob_cmd_append(cmd, "-Wno-deprecated-declarations");
    nob_cmd_append(cmd, "-fdata-sections");
    nob_cmd_append(cmd, "-ffunction-sections");
    nob_cmd_append(cmd, "-Werror=return-type");
    nob_cmd_append(cmd, "-D__USE_POSIX");
    nob_cmd_append(cmd, "-D_XOPEN_SOURCE=600");
    nob_cmd_append(cmd, "-fms-compatibility");

    if (debug) {
        nob_cmd_append(cmd, "-fsanitize=address");
        nob_cmd_append(cmd, "-ggdb");
    } else {
        nob_cmd_append(cmd, "-Os");
    }
}

static void ldflags(Nob_Cmd* cmd, bool debug) {
    nob_cmd_append(cmd, "-Wl,--gc-sections");
    nob_cmd_append(cmd, "-Wl,--as-needed");

    if (debug) {
        nob_cmd_append(cmd, "-fsanitize=address");
    } else {
        nob_cmd_append(cmd, "-Os");
    }
}

static bool rebuild_c(project_info project, const char* source_file, const char* object_file, bool debug) {
    bool result = true;
    Nob_Cmd cmd = {0};

    nob_cmd_append(&cmd, CC);
    cflags(&cmd, debug);
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
    ldflags(&cmd, true);
    nob_cmd_append(&cmd, "-fwhole-program");
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
            rebuild_c(project, source_file, object_file, true);
        }
    }

    nob_return_defer(nob_cmd_run_sync(cmd));

defer:;
    nob_cmd_free(cmd);
    nob_da_free(dependencies);
    nob_temp_rewind(checkpoint);
    return result;
}

static bool run_fchk(bool rebuild) {
    if (!build_project(layec0_driver_project)) {
        return false;
    }

    Nob_Cmd cmd = {0};

    if (!nob_file_exists("test-out")) {
        nob_cmd_append(&cmd, "cmake", "-S", ".", "-B", "test-out", "-DBUILD_TESTING=ON");
        if (!nob_cmd_run_sync(cmd)) {
            return false;
        }

        cmd.count = 0;
        nob_cmd_append(&cmd, "cmake", "--build", "test-out");
        if (!nob_cmd_run_sync(cmd)) {
            return false;
        }
    } else if (rebuild) {
        cmd.count = 0;
        nob_cmd_append(&cmd, "cmake", "--build", "test-out");
        if (!nob_cmd_run_sync(cmd)) {
            return false;
        }
    }

    cmd.count = 0;
    nob_cmd_append(&cmd, "ctest", "--test-dir", "test-out", "-j`nproc`", "--progress");
    if (!nob_cmd_run_sync(cmd)) {
        return false;
    }

    return true;
}

static bool ensure_compilers_are_rebuilt() {
    bool result = true;

    if (!build_project(layec0_driver_project)) {
        nob_return_defer(false);
    }

    if (!build_project(ccly_driver_project)) {
        nob_return_defer(false);
    }

    if (!build_project(laye_compiler_driver)) {
        nob_return_defer(false);
    }

defer:;
    return result;
}

static bool nob_build(bool explicit, int argc, char** argv) {
    bool result = true;

    if (!ensure_compilers_are_rebuilt()) {
        nob_return_defer(false);
    }

defer:;
    return result;
}

static bool nob_run(int argc, char** argv) {
    bool result = true;

    int build_argc = argc;
    char** build_argv = argv;

    Nob_Cmd run_cmd = {0};

    for (int i = 0; i < argc; i++) {
        if (0 == strcmp("--", argv[i])) {
            build_argc = i;
        }
    }

    if (build_argc != argc) {
        argc = argc - build_argc;
        argv += build_argc + 1;
    }

    if (!nob_build(false, build_argc, build_argv)) {
        nob_return_defer(false);
    }

    nob_cmd_append(&run_cmd, "./out/layec0");
    nob_da_append_many(&run_cmd, argv, argc);

    if (!nob_cmd_run_sync(run_cmd)) {
        nob_return_defer(false);
    }

defer:;
    nob_cmd_free(run_cmd);

    return result;
}

static bool nob_test(int argc, char** argv) {
    bool result = true;

    if (!ensure_compilers_are_rebuilt()) {
        nob_return_defer(false);
    }

    if (!build_project(exec_test_runner_project)) {
        nob_return_defer(false);
    }

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "./out/exec_test_runner");
    if (!nob_cmd_run_sync(cmd)) {
        nob_return_defer(false);
    }

    if (!run_fchk(false)) {
        nob_return_defer(false);
    }

defer:;
    return result;
}

#define delegate_to_command(SHIFT, CMD, ...)                       \
    do {                                                           \
        if (SHIFT) { nob_shift_args(&argc, &argv); }               \
        return CMD(__VA_ARGS__ __VA_OPT__(, ) argc, argv) ? 0 : 1; \
    } while (0)

int main(int argc, char** argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    int result = 0;

    const char* program = nob_shift_args(&argc, &argv);

    if (argc > 0) {
        const char* maybe_command = argv[0];

        if (0 == strcmp("build", maybe_command)) {
            delegate_to_command(true, nob_build, true);
        } else if (0 == strcmp("run", maybe_command)) {
            delegate_to_command(true, nob_run);
        } else if (0 == strcmp("test", maybe_command)) {
            delegate_to_command(true, nob_test);
        }
    }

    delegate_to_command(false, nob_build, false);
}
