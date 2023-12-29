# Laye
A systems programming language that wears its love for C on its shoulder.

## Philosophy
C is by no means perfect, nor is Laye trying to be perfect in its stead. Rather, Laye understands that C is here to stay for the foreseeable future, and wants to peacefully co-exist with established C code while supporting modern and (hopefully) sensible language features to the table. To achieve this, Laye allows seamless importing of C source files, most commonly the public headers, into any Laye source file. In addition, Laye provides lightweight FFI to expose its own functions and data types to C.

## Requirements
* `Clang 18` (or any compiler suitable for `C23`)
* `CMake` (only for [testing](#testing))

**NOTE**: The build system does **not** currently allow configuring the desired C compiler. This will be remedied in a future update. Until then, Clang is the expected compiler.

## Building
### 1. Build Nob
This project uses a version of the [Nob](https://github.com/tsoding/nobuild) build system by [Tsoding](https://github.com/tsoding) included with this repository.

```bash
$ clang -o nob nob.c
```

### 2. Run Nob
`Nob` will automatically rebuild itself if any changes are made to the build script.

```bash
$ ./nob
```

The `layec` compiler driver as well as a `test_runner` for running the automated tests are placed in the `./out` directory.

## Usage

```bash
$ ./out/layec [options...] files...
```

Run `./out/layec --help` for a list of avaliable options.

## Testing
The test suite is written for the [`fchk`](https://github.com/Sirraide/fchk) tool, which has a few additional dependencies as listed in [requirements](#requirements).

```bash
$ ./out/test_runner
```

Tests are located in the `./test` directory. Each test file is a source file which defines the test inline, as well as how it should be invoked by the tooling. Read the [`fchk`](https://github.com/Sirraide/fchk) documentation to understand how it works.

## Editor Support
Laye has a Visual Studio Code extension for syntax hilighting. Find it [here](https://github.com/laye-lang/laye-vscode).
