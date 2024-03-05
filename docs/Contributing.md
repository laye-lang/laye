## Using Laye in new projects

One of the best ways to contribute to Laye is to use it in projects where you're able; don't use Laye in production just yet! New hobby projects are the best place to give it a shot for now. If you're up to it, then please share your thoughts on the language and its provided tooling (you can email laye@nashiora.com or join the Forever Untitled [Discord server](https://discord.gg/paKyDFKJtr) to most easily reach me personal; my name is Local, it's nice to meet you!). Laye has a *long* way to go before it's ready to be considered a user-friendly experience, with all of the modern tooling expected of a language, but one way to help make that process quicker and smoother is to provide early feedback from personal use.

If you encounter any problems or want to propose a new feature, consider opening an issue!

## Opening a new issue

As with simply using the project, many people overlook opening issues as valid contribution to a project. Opening issues is vital to tracking user experience issues and software bugs, so even if you're not currently able to work on the source code itself it's still incredibly valuable to have user experience issues tracked!

When opening an issue, keep some things in mind:
- The title should be as concise and meaningful as you can make it. For example, if you notice odd behavior in the `+` operator when adding an integer and a float, but not when adding two integers, you might create an issue titled "Operator '+' has inconsistent behavior for some primitive types" or "int + float produces incorrect result". You could maybe get a bit more specific, but a title isn't the place to outline every part of the issue in detail.
- Provide as much context as you're able to in the main body of the issue. If you've tried other related things that don't have this issue, please include them as well for comparison. If you get specific error messages or assertions, include them! You don't need to include every detail under the sun, but it can be hard to track down issues if you don't provide details about what you're up to.
- In addition to these kinds of details, *please* provide reproduction instructions! This may mean you have to do a bit of extra work to simplify something to its core while still maintaining the erroneous behavior. Reproduction is the most important part of an issue. If a developer can't reproduce the issue, they're shooting in the dark trying to fix it.

If you're having trouble putting together an issue meeting the above requirements, do try to submit it anyway! Do your best and maybe leave a note that you're looking for help gathering the information. I'd much rather see an issue with a long title and bad description if it includes willingness to update it if guided through the process than not have the issue present at all.

There will be a line where an issue is actively not helpful, but until the project grows enough for it to happen at volume I'm not concerned.

## Contributing to the Laye compiler

If you'd like to contribute code to the project, I'm more than happy to review a pull request and look at merging your work. There are a few things to keep in mind before you do, though.
- If you want to be sure your time is spent in a manner that will make your pull request more likely to get merged, check out the open GitHub issues. An open issue means code updates addressing it are likely to be accepted, especially so if there's discussion on it outlining possible solutions.
- If you want to make a change to the Laye language syntax or semantics, run the desired change through me, Local (nashiora on GitHub), first. I've got some pretty narrow design goals for the project that I'm not very flexible on, and most of those things are not well documented. I'd feel bad turning down a bunch of PRs for otherwise interesting implementations just because they're not the direction I want this project in particular to go.
- Follow the existing conventions in the source code. Run ClangFormat if you can, or otherwise do your best to match the existing formatting. Use the same naming style and conventions: basically everything is `snake_case`, and global names are prefixed with relevant words to emulate namespacing. Use the same primitive types as the rest of the code: for example, `int64_t` is preferred to `size_t` unless directly interfacing with a libc function that uses `size_t`.

I want you to contribute something you enjoyed working on, but I don't want to sour the work by rejecting an otherwise exciting pull request or get lost in long and frustrating code review sessions in discussions. To save us all our time and sanity, do a little extra work up front to make the contribution more likely to succeed.

When contributing code, you should also be okay with your work being licensed with the project. The source code for the Laye tools in this repository are dual licensed under the MIT license and the public domain.

### Adding tests

When contributing code that changes externally-visible behavior, tests should probably be added or updated as well. The `./test` directory contains language source files with inline test results for the automated testing tools to compare against. You'll be updating an existing source file that already tests similar functionality (or the functionality that you specifically modified) or creating a new test file.

The test files are checked by two separate testing tools. The most important one is Sirraide's [FCHK](https://github.com/sirraide/fchk). For a complete reference on how to use FCHK, read the documentation in the project's repository.

In most cases, *both* kinds of test exist in the exact same source file.

*NOTE: It's very likely that the execution tests will be phased out or otherwise significantly changed in the near future. The FCHK tests are the more valueable of the two tests, and checking just the exit code of a program is not a very useful test on its own; it was added originally to verify that compilation past LYIR yielded the correct executable code, but nontrivial programs should probably be tested against more than just an exit code.*

#### Execution tests

The easiest of the two tests to understand is the execution test. The only requirement is that the first line of the source file be a comment with the expected exit code for the program when run. The exit code can come either from the return value of `main` or from a call to the `exit` syscall (usually through libc's `exit` function).

```c
// 69
int main() {
    return 69;
}
```

#### FCHK tests

The simplest FCHK test needs two things in addition to the source code under test:
1. A test run directive.
2. The expected LYIR output, inline in the source text.

Both of these things will exist within comments in the source code. FCHK will read through the comments to determine what to run and what to compare against. Here's a simple example for illustration:

```c
// R %layec -S -emit-lyir -o %s.lyir %s && cat %s.lyir ; rm %s.lyir

// * define exported ccc main() -> int64 {
// + entry:
// +   return int64 0
// + }
int main() {
    return 0;
}
```

The first line is the run directive, denoted by the capital `R`. Everything following the `R` is the shell command that should be used to invoke this test, populating `stdout` with the actual content to test. In most cases, we'll be invoking the Laye compiler and generating LYIR output. This output goes to a file, not `stdout`, so we `cat` it so FCHK can see it. Removing the file afterward is a courtesy to the developer; a messy workspace is no fun!

*NOTE: These kinds of commands are not easily portable across operating systems or shell environments. In the near future, outputing a LYIR module to `stdout` will be standardized within the compiler itself.*

The contents of `stdout` are then checked line by line against the other two directives: `*` and `+`. When a `*` directive is encountered, FCHK searches for a line which matches it anywhere in the `stdout` contents. If no match is found, the test produces an error. When a `+` directive is encountered, FCHK checks the line after the previously matched line in the same manner. A `*` directive followed by a number of `+` directives indicates that an unbroken sequence of lines is expected to match exactly as indicated. Since a `*` directive searches anywhere in the `stdout` contents, they can be out of order from the actual output.

#### Running the tests

You can invoke the entire test suite by running the following build command:

```bash
$ ./nob test
```

This will first run the execution tests, then the FCHK tests.

*NOTE: eventually, the Nob build tool will support running only a specific test. This is not currently easily supported for FCHK, but a refactor of how FCHK is invoked will be happening soon, making the change much more feasible.*

Currently, some tests are expected to fail because the project is in a rapid development state (and I'm bad at waiting to add tests until a feature is ready). It is recommended that you run the whole test suite before making any changes to know which tests currently fail. If a test fails before you make a change, you are not responsible for it.

## Contributing to the GitHub Wiki

The documentation (stored in the `docs` directory) is a git subtree of the [GitHub project wiki](https://github.com/laye-lang/laye/wiki). This allows for the documentation to be referenced and edited from within the main project.

*Thanks to [this gist](https://gist.github.com/yukoff/5220f33123de5e7e428db63ef7025e72) for documentation on how to make this work!*

### Initial local setup

When cloning the main project repository for the first time, the wiki repository must be added as a remote.

```sh
$ git remote add wiki git@github.com:laye-lang/laye.wiki.git
```

### Adding documentation

New and updated documentation should be committed and pushed in the main project repository just like any other file.

*However, care should be taken to ensure documentation files are always committed separately from other project files. Individual commits should never contain both files inside and outside the docs directory.*

For most contributors, this is all you have to worry about. Make your commits with *only* the `docs` folder changes present and submit a pull request. Interacting directly with the wiki's repository is not something you're likely to have access to. If you do, read on.

### Pushing updates to the wiki

Documentation changes in the main project repository can be pushed to the wiki repository at any time. This does not need to happen every time documentation is added or updated, but can happen as often as desired.

```sh
$ git subtree push --prefix docs wiki master
```

If the GitHub website is used to edit wiki files, then the push will fail, in which case any updates should be pulled as described below.

### Pulling updates from the wiki

It should only be necessary to pull updates directly from the wiki when the GitHub website is used to edit the documentation directly in the wiki. If pushing to the wiki ever fails, try pulling first.

```sh
git subtree pull --prefix docs wiki master --squash --message="Merge wiki updates into docs."
```
