<br/><br/><br/><br/><br/><br/><br/><br/>

# This is the GitHub page for code development. If you just want to download the latest spice2x EXE, visit the homepage instead:

# 🌶️🌶️ [https://spice2x.github.io/](https://spice2x.github.io/) 🌶️🌶️

<br/><br/><br/><br/><br/><br/><br/><br/>

# spice2x

## Overview

spice2x is a fork of SpiceTools, focused on addressing bugs, usability, and quality-of-life improvements.

We do not use GitHub for source control, but we do use the [issue tracker](https://github.com/spice2x/spice2x.github.io/issues) and [the wiki](https://github.com/spice2x/spice2x.github.io/wiki). Source is distributed in the release package.

spice2x team does not provide any tools to circumvent software copy protection, nor distribute any copyright-protected game data.

[Features](https://github.com/spice2x/spice2x.github.io/wiki/spice2x-features)

[List of supported games](https://github.com/spice2x/spice2x.github.io/wiki/List-of-supported-games)

## Submitting to the Issue Tracker
Rules for filing a new issue or adding comments to existing issues in the tracker:

* Check the [known issues](https://github.com/spice2x/spice2x.github.io/wiki/Known-issues) page first before reporting a new issue.
* Use the search function and see if there is an existing issue. 
* This is not the place to ask about other projects, especially EA servers. Bad servers can (and will) crash your game.
* **Do not link to external websites that distribute game data!**

New GitHub accounts are prevented from creating new issues to prevent spam.

Maintainers of this project reserve the right to close or delete any low effort issues.

## Contributing
**We encourage the community to submit patches via the issue tracker for any bug fixes or feature enhancements.** If you want to resolve any reported (or not reported) bugs, implement features, add support for new games, or fix a [known issue](https://github.com/spice2x/spice2x.github.io/wiki/Known-issues) - feel free to reach out via the Issue tracker. All submitted code patches are assumed to be GPLv3 compliant.

Baseline rules for patch submissions are as follows. Any patches violating the rules below will not be accepted.

### Adding support for games

* No BomberGirl, as long as the Free To Play service is still alive.
* No support for that one touch screen game that dispenses plushies of mouse character (`O26`).
* No support for eaCloud ("Konasute") games.
* For a new game that was never supported (not a new version of a game, but rather a new series): please wait 1 year after official AC release in Japan.
* For new version of an already supported game: proceed with caution and use generally-accepted community guidelines.

### Code quality requirements

* Test for regressions, at least in and around the component you are modifying:
  * All currently supported games / versions must continue to work.
  * Backwards compatibility must be preserved, unless there is a really good reason to break it. This includes (but not limited to): global/local config files, command line parameters, game patches, device interop (e.g., card readers), and SpiceAPI / Companion interop.
  * Reasonable level of compatibility with the last release of original spicetools is expected. The stated goal of spice2x is to be a drop-in replacement for spicetools.
  * Simply put, if someone has an existing install of spicetools/spice2x, copying over new version of spice2x should not result in different behavior or loss of functionality.
* Make sure you compile with the included Docker script and ensure you do not introduce **any** new compiler warnings or build breaks. The Docker script is the standard build environment, your custom Linux build environment or MSVC can be used during development, but you must validate the final build using Docker.
* Do not submit snippets of code as a "patch". Exceptions can be made for trivial changes (correct a typo, fix a single line of code...), but otherwise, a successfully compiled & fully tested patch file is required when submitting for review.
* Do not make code changes in unrelated areas; i.e., do not run code linters and auto-formatters for parts of the code that you didn't modify.
  * Note: there are no strict rules for code formatting, but please attempt to emulate the style around the code you are modifying.
