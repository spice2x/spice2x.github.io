## Contributing
Baseline rules for patch submissions are as follows. Any patches violating the rules below will not be accepted.

### Adding support for games

* No BomberGirl, as long as the Free To Play service is still alive.
* No support for that one touch screen game that dispenses plushies of mouse character (`O26`).
* No support for eaCloud ("Konasute") games.
* For a new game that was never supported (not a new version of a game, but rather a new series): please wait 1 year after official AC release in Japan.
* For new version of an already supported game: proceed with caution and use generally-accepted community guidelines.

### Avoiding regressions

The biggest risk in making code changes to spice is the risk of regressions. There is a **lot** of shared code used by many different games, reaching back game versions that are more than a decade old. It is practically impossible to test all supported games and versions.

Additionally, there's a lot of code that get exercised on specific hardware - including hardware that you probably don't have access to (e.g., ICCA reader, real cabinet I/O, specific model of a IIDX controller, particular brand of touch screens...)

Therefore, when making code changes, please by extremely careful about containing / scoping your changes. Make targeted bug fixes scoped to handful of game versions and hardware configuration. When adding new features, make it off by default, unless there is a really good reason to make it the default. If you make a new default, add an option that disables it so that users can opt out as needed.

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
* Try to submit smaller chunks of code, instead one gigantic patch. For example, don't submit a patch for "Improve feature XYZ"; instead, submit "Change how A works to prepare for feature XYZ" "Refactor B for feature XYZ" "Add feature B to enable feature XYZ".
