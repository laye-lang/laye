# Laye

## How to Build

```bash
$ clang -o nob nob.c
$ ./nob
```

## Dependencies and Requirements

The Laye compiler generates LLVM IR expecting Clang 18. LLVM IR is not stable between Clang versions, so this is considered a firm requirement. You may have luck with a different version, but it is not guaranteed.

## Editor Support

Laye has a Visual Studio Code extension for syntax hilighting. Find it [here](https://github.com/laye-lang/laye-vscode).
