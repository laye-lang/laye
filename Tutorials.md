The tutorials will show Linux-style terminal commands. If you're on Windows, note that:
- the argument to the `-o` flag should probably have `.exe` extension added.
- running the output file won't *require* `./` since Windows terminals will also search the current working directory for executables by default.

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
$ layec -o hello hello.laye
$ ./hello
Hello, hunter!
```

<hr />

For the time being, Laye relies heavily on the C standard library for its core functionality. Periodically check back in with the docs to see what's changed with the Laye standard library in guides and tutorials like this.

Eventually it will be possible to `import` C header files, meaning you could then instead `import "stdio.h";` and refer to `printf` directly from your system headers.
