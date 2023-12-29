# Laye

Laye is a systems programming language that wears its love for C on its shoulder.

C is by no means perfect, nor is Laye trying to be perfect in its stead. Rather, Laye understands that C is here to stay for the foreseeable future, and wants to peacefully co-exist with established C code while supporting modern and (hopefully) sensible language features to the table. To achieve this, Laye allows seamless importing of C source files, most commonly the public headers, into any Laye source file. In addition, Laye provides lightweight FFI to expose its own functions and data types to C, though using Laye from C sadly requires a bit more work since C is blissfully unaware of Laye's existence.

## Dependencies and Requirements

The Laye compiler generates LLVM IR expecting Clang 18. LLVM IR is not guaranteed to be stable between Clang versions, so this is considered a firm requirement. You may have luck with a different version, but it is not guaranteed.

This repository expects your compiler to support the C23 standard. Clang 18 supports this, but any C23 compliant C compiler should do. **NOTE: Even though any C23 compliant compiler should work, the build system does not currently allow configuring the desired C compiler. This will be remedied in a future update. Until then, Clang is the expected compiler.**

## How to Build

The goal for the Laye compiler is to always be able to build on any system with a (standard compliant) C compiler. In practice that means most desktop environments should be supported by default, but no guarantees are currently made for less widely used systems. There should never be any external dependencies otherwise.

This project uses a version of the Nob build system by Tsoding. At the time of writing, the new Nob project is not officially hosted, so the build system might be considered "outdated". Never fear, the build system's code is included with this repository and is very easy to build.

```bash
$ clang -o nob nob.c
$ ./nob
```

The `./nob` command is self-rebuilding, so if any changes are made to the build script itself, it will automatically be rebuilt and re-run itself without you even needing to know.

Running the Nob build will generate the `layec` compiler driver as well as a `test_runner` for running the automated tests. The build artifacts are placed in the `./out` directory.

## Running the Tests

The test runner has a few additional requirements that the compiler itself does not. The test suite is written for the [fchk](https://github.com/Sirraide/fchk) tool, which has its own requirements to build. So long as your system has the appropriate tooling installed, the `test_runner` executable will handle cloning, building and running `fchk` for you when you run it. These dependencies are not required to build the test runner, so don't worry if you don't have the prerequisites; so long as you don't intend to run the tests, you're all good to go.

Tests are located in the `./test` directory. Each test file is a source file which defines the test inline, as well as how it should be invoked by the tooling. Read the [fchk](https://github.com/Sirraide/fchk) documentation or README to understand how it works.

## Editor Support

Laye has a Visual Studio Code extension for syntax hilighting. Find it [here](https://github.com/laye-lang/laye-vscode).
