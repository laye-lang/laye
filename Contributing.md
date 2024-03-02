## Opening a new issue

Opening an issue is just as much contribution as anything else is, even though most people gloss over it when talking about contributing to a project.

## Contributing to the Laye compiler

TODO write this

### Adding tests

TODO also write this

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
