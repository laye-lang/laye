cmake_minimum_required(VERSION 3.15)

project(layec
    VERSION 0.1.0
    LANGUAGES C
)

if (MSVC)
    add_compile_options(
        /W4 # Enable all warnings.
        /wd4201 # Allow unnamed structs/unions.
        # Don't suggest the _s nonsense functions.
        /D_CRT_SECURE_NO_WARNINGS
        /D_CRT_SECURE_NO_WARNINGS_GLOBALS
        /D_CRT_NONSTDC_NO_WARNINGS
    )
else()
	add_compile_options(-Wall -Werror=return-type -Wshadow -Wconversion -Wno-unused -fsanitize=address -g)
	add_link_options(-fsanitize=address)
endif()

add_executable(layec "${PROJECT_SOURCE_DIR}/src/driver.c")
target_include_directories(layec PUBLIC "${PROJECT_SOURCE_DIR}/include")
