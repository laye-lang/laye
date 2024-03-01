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

If you're on Windows, you probably want to include the `.exe` extension on the output file and just run `hello`, no need for the `./` prefix. Other tutorials and guides will expect you to know how to use your own command line tools correctly, and these little differences will not be outlined each time.

<hr />

For the time being, Laye relies heavily on the C standard library for its core functionality. Periodically check back in with the docs to see what's changed with the Laye standard library in guides and tutorials like this.

Eventually it will be possible to `import` C header files, meaning you could then instead `import "stdio.h";` and refer to `printf` directly from your system headers.
