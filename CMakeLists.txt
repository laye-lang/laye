cmake_minimum_required(VERSION 3.20)
project(layec-test-runner)

## ============================================================================
##  Tests
## ============================================================================
# Allow arbitrary characters in test names.
cmake_policy(SET CMP0110 NEW)

add_compile_options(-stdlib=libstdc++)
add_compile_options(-cxx-isystem /usr/local/clang+llvm-18.1.8/include/c++/v1/)

if (BUILD_TESTING)
    include(ExternalProject)
    include(CTest)

    ## Set fchk properties here.
    if (DEFINED FCHK_GIT_TAG)
      message(
        FATAL_ERROR
        "You've defined FCHK_GIT_TAG as a user-specified option, but you must alter the CMakeLists.txt to change this variable"
      )
    endif()
    set(FCHK_GIT_TAG master)

    ## First, check if fchk has already been downloaded.
    if (NOT EXISTS "${PROJECT_SOURCE_DIR}/fchk/CMakeLists.txt")
        message(STATUS "Downloading fchk")

        ## Delete old fchk directory.
        if (EXISTS "${PROJECT_SOURCE_DIR}/fchk")
            file(REMOVE_RECURSE "${PROJECT_SOURCE_DIR}/fchk")
        endif()

        ## Clone the repo.
        execute_process(
            COMMAND git clone https://github.com/Sirraide/fchk
                --branch "${FCHK_GIT_TAG}"
                --depth 1
                --recursive
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
            ECHO_OUTPUT_VARIABLE
            ECHO_ERROR_VARIABLE
        )
    endif()

    ## Next, update it to the right tag.
    message(STATUS "Updating fchk")
        execute_process(
        COMMAND git fetch --tags
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/fchk"
        COMMAND_ERROR_IS_FATAL ANY
        ECHO_OUTPUT_VARIABLE
        ECHO_ERROR_VARIABLE
    )

    execute_process(
        COMMAND git fetch --depth 1 origin "${FCHK_GIT_TAG}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/fchk"
        COMMAND_ERROR_IS_FATAL ANY
        ECHO_OUTPUT_VARIABLE
        ECHO_ERROR_VARIABLE
    )

    execute_process(
        COMMAND git reset --hard "${FCHK_GIT_TAG}"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/fchk"
        COMMAND_ERROR_IS_FATAL ANY
        ECHO_OUTPUT_VARIABLE
        ECHO_ERROR_VARIABLE
    )

    ## We only need to download fchk to be able to configure this,
    ## so add a target for the tests to build it when we run the tests.
    add_custom_target(fchk ALL
        COMMAND ${CMAKE_COMMAND}
            -S "${PROJECT_SOURCE_DIR}/fchk"
            -B "${PROJECT_SOURCE_DIR}/fchk/out"
            -G "${CMAKE_GENERATOR}"
        COMMAND ${CMAKE_COMMAND} --build "${PROJECT_SOURCE_DIR}/fchk/out"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/fchk"
        BYPRODUCTS "${PROJECT_SOURCE_DIR}/fchk/out"
        VERBATIM
        USES_TERMINAL
    )

    ## Now that that’s done, we can set up the tests.
    set(FCHK_EXE_PATH "${PROJECT_SOURCE_DIR}/fchk/out/fchk")
    include("${PROJECT_SOURCE_DIR}/fchk/cmake/FCHKDiscoverTests.cmake")

    ## Add more tests below if need be.
    FCHKAddAllTestsInDir(
        IN "test/laye"
        PATTERN "*.laye"
        PREFIX "//"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        ARGS -l . -D "layec=./out/layec0" -P re -P nocap
        RECURSIVE
        DEPENDS fchk
    )
endif()
