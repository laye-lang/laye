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

You can then run the `nob` executable to run the default build process. To install the Laye compiler and its standard library, run the following:

```sh
$ ./nob install
```

By default, `./nob install` will attempt to install Laye to your default system install directories. On Windows, this is `C:\Program Files\Laye` and on Linux this is `/usr/bin` and `usr/lib/laye`. You can pass the `--print-dirs` option to print the installation directories and exit before installation occurs to verify the install paths are to your liking.

To change where Laye is installed, specify an install prefix manually.

```sh
$ ./nob install --prefix ~/.laye
```

This will place the `laye` compiler executable in `~/.laye/bin` and the standard library source files in `~/.laye/lib/laye`. To control each install location separately, pass both the `--bin-prefix` and `--lib-prefix` options instead.

```sh
$ ./nob install --bin-prefix ~ --lib-prefix /usr/share
```

To clear any ambiguity in how the final install paths are constructed:
- The `bin` directory name is appended to value of the `--bin-prefix`.
- The compiler executable name is `laye`, so the final location for it is `$BIN_PREFIX/bin/laye`.
- The `lib` directory name is appended to value of the `--lib-prefix`.
- The standard library files are contained within another subdirectory named `laye`. The final location for it is then `$LIB_PREFIX/lib/laye/<library-files>`.
- The `--prefix` argument performs the exact same logic for both directories at the same time.

*The compiler currently does not implicitly include the standard library. While there is a simple plan to make this possible, for the time being you must explicitly pass `-I $LIB_PREFIX/lib/laye/<lib-name>` to all compiler invorations. The name of the standard library directory is `liblaye`.*
