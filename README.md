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

Contributions to this project are always welcome! If you want to add or change anything, feel free to `fork` this repository. After applying your changes, create a `pull request` with a meaningful title and detailed description. Also, please make sure to pay attention to `requested changes` and `reviews` from other contributors. Thanks in advance!

## Goals

High level goals, in roughly the order we aim to achieve them:

1. [ ] Lex C source without a preprocessor.
2. [ ] Apply a subset of the preprocessor to C while lexing.
3. [ ] Successfully lex the project's source.
4. [ ] Lex Laye source.
5. [ ] Successfully lex the project's source again.
6. [ ] Parse subset of C source necessary for a simple but complete C program.
7. [ ] Successfully parse the project's source.
8. [ ] Parse Laye source.
9. [ ] Successfully parse the project's source again.

If we can continue to parse the project's source after implementing a parser for both Laye and C, then we're pretty much home free for continuing to be able to self-host this project through to code generation. Self-hosting in Laye is not a goal of this project.

Being able to parse other C projets is an entirely different beast. Along the way we'll probably add support for popular extensions, but supporting enough extensions to be able to compile popular libraries in full may take much longer than self-hosting.
