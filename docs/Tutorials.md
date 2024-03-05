The tutorials will show Linux-style terminal commands. If you're on Windows, note that:
- the argument to the `-o` flag should probably have `.exe` extension added.
- running the output file won't *require* `./` since Windows terminals will also search the current working directory for executables by default.

It is also assumed that Laye is installed as a package, or you've run the [install](Installation) steps from the source tree and have `laye` in your system path or otherwise accessible.
If you want to run the compiler straight from the source tree for development purposes, you can replace calls to `laye` with either `./nob run -- ...` to ensure the compiler is always rebuilt, or `./out/laye1 ...` to invoke an already-built stage1 executable.

## Hello, hunter!

The most important step when using a new programming language, or even just when starting a new project, is setting up the famous "Hello, world" program. In Laye, that looks like this:

```cpp
import "libc.laye";

void main() {
    libc::printf("Hello, hunter!\n");
}
```

Save this code as `hello.laye`, then compile and run it with the following commands.

```sh
$ laye -o hello hello.laye
$ ./hello
Hello, hunter!
```

<hr />

For the time being, Laye relies heavily on the C standard library for its core functionality. Periodically check back in with the docs to see what's changed with the Laye standard library in guides and tutorials like this.

Eventually it will be possible to `import` C header files, meaning you could then instead `import "stdio.h";` and refer to `printf` directly from your system headers.

## Importing other Laye source files

It'd be pretty annoying having to write all of your code in just a single source file. The "Hello, hunter" example showed off Laye's ability to import other source files and reference their exported declarations through a namespace.

The simplest use of `import` is simply to provide it a file path. The file path can be relative to the current file, or relative to any of the "include" directories given to the compiler.

In the following example, both source files exist within the same directory. It's trivial, then, to include one from the other.

```cpp
// in file `./my_math.laye`
export int add_mul(int a, int b, int c) {
    return (a + b) * c;
}

// in file `./main.laye`
import "my_math.laye";
void main() {
    // result should contain the value 420
    int result = my_math::add_mul(23, 19, 10);
}
```

`main.laye` is able to find the file `my_math.laye` next to it, and creates a namespace of the same name ("my_math") which contains all of its exported declarations. The double-colon syntax allows looking up names within namespaces, so `my_math::add_mul` is able to search for the exported function.
