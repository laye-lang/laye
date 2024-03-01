For the time being, Laye relies heavily on the C standard library for its core functionality.

```
import "libc.laye";

void main() {
    libc::printf("Hello, hunter!\n");
}
```

Eventually it will be possible to `import` C header files, meaning you could then instead `import "stdio.h";` and refer to `printf` directly from your system headers.
