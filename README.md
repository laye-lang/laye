# Laye

Compiler for the Laye programming language.

Laye is a programming language designed to be used alongside C. Where many languages aim to be a C replacement, Laye instead wants to co-exist in its own unique manner. This relationship is so important that the Laye compiler, `layec`, is also a C compiler.

This project is an attempt to create a compiler completely from scratch (read: using nothing other than C and its standard library) for the Laye programming language and for as much of C as we can manage. It's entirely possible that the goal of being a mostly-featured C compiler is too lofty, and the ability to use C files may be relegated to other compilers like Clang.

## Usage

Running the compiler with no arguments (or with the `--help` flag) will display a usage message detailing compiler flags and command layout.

## Building

Dependencies:
- CMake >= 3.22
- Any C Compiler (We like Clang)

```
cmake -B build
cmake --build build
```

## Contributing

[TODO]
