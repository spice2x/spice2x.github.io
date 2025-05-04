## Contributing
Baseline rules for patch submissions are as follows. Any patches violating the rules below will not be accepted.

### New as of March 2025
Pull requests in GitHub are open. You don't need to bother with patch file submissions anymore. To contribute, fork the repo (just the main branch), make changes in your fork, and contribute to upstream by opening a pull request to the main repo.

### Adding support for games

* No BomberGirl, as long as the Free To Play service is still alive.
* No support for that one touch screen game that dispenses plushies of mouse character (`O26`).
* No support for eaCloud ("Konasute") games.
* For a new game that was never supported (not a new version of a game, but rather a new series): please wait 1 year after official AC release in Japan.
* For new version of an already supported game: proceed with caution and use generally-accepted community guidelines.

### Avoiding regressions

The biggest risk in making code changes to spice is the risk of regressions. There is a **lot** of shared code used by many different games, reaching back game versions that are more than a decade old. It is practically impossible to test all supported games and versions.

Additionally, there's a lot of code that get exercised on specific hardware - including hardware that you probably don't have access to (e.g., ICCA reader, real cabinet I/O, specific model of a IIDX controller, particular brand of touch screens...)

Therefore, when making code changes, please be extremely careful about containing / scoping your changes. Make targeted bug fixes scoped to handful of game versions and hardware configuration. When adding new features, make it off by default, unless there is a really good reason to make it the default. If you make a new default, add an option that disables it so that users can opt out as needed.

#### Config file compatibility

Do not change the names of options, buttons binds, analogs, etc - since they are used as unique identifiers in config files. If they are changed, you will introduce an incompatibility with previous versions of config file.

### Code quality requirements

* Test for regressions, at least in and around the component you are modifying:
  * All currently supported games / versions must continue to work.
  * Backwards compatibility must be preserved, unless there is a really good reason to break it. This includes (but not limited to): global/local config files, command line parameters, game patches, device interop (e.g., card readers), and SpiceAPI / Companion interop.
  * Reasonable level of compatibility with the last release of original spicetools is expected. The stated goal of spice2x is to be a drop-in replacement for spicetools.
  * Simply put, if someone has an existing install of spicetools/spice2x, copying over new version of spice2x should not result in vastly different behavior or major loss of functionality.
* Make sure you compile with the included Docker script and ensure you do not introduce **any** new compiler warnings or build breaks. The Docker script is the standard build environment, your custom Linux build environment or MSVC can be used during development, but you must validate the final build using Docker.
* Do not make code changes in unrelated areas; i.e., do not run code linters and auto-formatters for parts of the code that you didn't modify.
* Try to submit smaller chunks of code, instead one gigantic patch. For example, don't submit a patch for "Improve feature XYZ"; instead, submit "Change how A works to prepare for feature XYZ" "Refactor B for feature XYZ" "Add feature B to enable feature XYZ".
* Write to the log for anything useful - it helps immensely with troubleshooting and debugging. At the same time though, avoid spamming the log for something trivial.

### UI text
spice2x has a global audience; majority of the user base do not speak English as their first language.

Use simple English, avoid colloquialism, and use concise language, even if it's slightly technical.

### Using OS APIs

Avoid making permanent changes to user's OS configuration. For example, spice must not make a call to set power profile to Maximum Performance, or switch default audio device. Making the reverse call to restore settings on game shutdown is **not** good enough; there is no guarantee that spice will gracefully shutdown, since games (or spice) can crash. This is to avoid inadvertently putting user's PC into a bad state, which can be seen as malware-like behavior.

Watch out for legacy OS compatibility. Currently, the minimum support floor is Windows 7. If you use any Windows API, make sure it's supported in Windows 7. If you need to use API that is not present in Windows 7, you must not directly link against it, otherwise spice will fail to launch on older OSes. Take a look at Windows 8 touch code (win8.cpp) for examples on how to discover OS APIs via pointers.

### Code style requirements
* Indents are four spaces.
* Always use \{ curly braces \} when appropriate; do not omit them even when it's optional; such as `for` `if` `else`, etc.

OK:
```c
if (conditional) {
    DoSomething();
}
```

Not OK:
```c
if (conditional)
    DoSomething();
```

Not OK:
```c
if (conditional) DoSomething();
```

* Opening curly braces should appear at the end, not in a line on its own.

OK:
```c
if (conditional) {
    DoSomething();
}
```

Not OK:
```c
if (conditional)
{
    DoSomething();
}
```

* Please give [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) a read. A few things to point out in spice codebase:
  * We don't use GSL.
  * Don't throw exceptions.
  * Stick to smart pointers. For interfacing with C Win32 API and raw buffers, use `unique_plain_ptr`.
  * Writing in C is also completely acceptable.
* Other than that, there are no strict rules for code formatting, but please attempt to emulate the style around the code you are modifying.
