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

## A simple Raylib application

[Raylib](https://www.raylib.com/) is a simple C library for creating games. In just a handful of function calls you'll have access to a window you can draw to and interact with. To follow along with this tutorial, make sure you've followed Raylib's installation guides to get the library set up on your system.

There are no official Raylib bindings currently, and importing C header files is also not yet supported, so we'll need to write our own bindings to the functions we want to use. "Bindings" refers to writing wrapping code in one language to provide access to functions written in another language. Laye makes this very simple, and will be making it even simpler for C libraries in the near future.

To start, let's create two Laye source files: `main.laye` and `raylib.laye`. We'll put all of our application logic in the main file, and put all of our hand-written Raylib bindings in the other.

To verify we have a working Raylib installation, we only need 5 functions:

```cpp
// raylib.laye
export foreign "InitWindow" void init_window(i32 width, i32 height, i8[*] title);
export foreign "WindowShouldClose" bool window_should_close();
export foreign "BeginDrawing" bool begin_drawing();
export foreign "EndDrawing" bool end_drawing();
export foreign "CloseWindow" void close_window();
```

By convention, Laye names are always `snake_case`. If you'd like your Laye code to look consistent and idiomatic, the `foreign` attribute can be used to effectively rename functions. `foreign "InitWindow"` tells the compiler that this function's real name is "InitWindow", regardless of what it's declared as. This means we can name the function whatever we want in Laye, but the compiler will know to look it up by a different name when linking against the library. We do this for all of the functions we want from Raylib.

The only function that takes arguments right now is `init_window`. Raylib only requires the width, height and window title. The original C declaration looks like this:

```c
void InitWindow(int width, int height, const char *title);
```

The equivalent of C's `int` type is Laye's `i32`, and the equivalent of a `const char*` string is `i8[*]`. Yes, Laye string literals can also function as C string literals!

Also note that we're marking each of these functions as exported. Without that, we wouldn't be able to then import them in our main program.

Speaking of, let's build out the simplest Raylib application we can and get it compiling.

```cpp
// main.laye
import "raylib.laye";

int main() {
    raylib::init_window(800, 600, "Raylib from Laye");
    while (not raylib::window_should_close()) {
        raylib::begin_drawing();
        raylib::end_drawing();
    }
    
    raylib::close_window();
    return 0;
}
```

With this, we've initialized a window and told it to run through its event processing until the user presses the close button.

You should now be able to compile the program, linking against Raylib:

```bash
$ laye -o raylib_example main.laye -lraylib
$ ./raylib_example
```

The `-l` option tells the compiler (or really, the linker) that we want to link against a specific library. If you've installed Raylib correctly, then `-lraylib` should be sufficient to link against Raylib.

Running the application should give you a nice, empty window you can drag around and close.
