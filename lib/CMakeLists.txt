cmake_minimum_required(VERSION 3.22)

file(GLOB_RECURSE liblayec-sources "*.c")
add_library(liblayec ${liblayec-sources})
target_include_directories(liblayec PUBLIC "${PROJECT_SOURCE_DIR}/include")

install(TARGETS liblayec
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib/liblayec)
