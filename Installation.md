## From source

You can build and install Laye tools from source.

If you haven't already, grab yourself a copy of the source code, either manually or through git.

```sh
$ git clone git@github.com:laye-lang/laye.git
$ cd laye
```

Laye uses a build script written in C using (a modified version of) the `nob.h` build tool from [Alexey Kutepov](https://tsoding.github.io/). To bootstrap the build tool, simply compile it with your favorite C compiler.

```sh
$ clang -o nob nob.c
```

You can then run the `nob` executable to run the default build process.

To install the Laye compiler and its standard library, run the following:

```sh
$ ./nob install BIN_DIR STDLIB_DIR
```

At the moment, no standardized configuration exists for installing the Laye compiler. The `install` command simply requires you pass a prefix for where it should place the `layec` binary and where to place the `liblaye` standard library source directory. For the time being, I recommend using `/usr/local/bin` or `~/bin` as the binary prefix and `~/.local/lib` as the library prefix.

The compiler currently does not implicitly include the standard library because of this half-baked installation ritual. It does not know where you installed the standard library, so you must pass `-I ~/.local/lib/liblaye` (or wherever you installed it) with all invocations of the compiler. When the install process is cleaned up and more standardized, this *will not* be required anymore; especially as OS packages are created, those will pre-configure all of these directories and build Laye knowing where they are.
