SpiceTools
==========
This is a loader for various arcade games developed by 573.
The project is using CMake as it's build system, with a custom build
script for making packages ready for distribution and to keep it easy
for people not knowing how to use CMake.

## Building/Distribution

We're currently using Arch Linux for building the binaries.
You'll need:
- MinGW-64 packages (can be found in the AUR)
- bash
- git
- zip
- upx (optional)

For any other GNU/Linux distributions (or Windows lol), you're on your
own.

To build the project, run:

    $ ./build_all.sh

## Build Configuration

You can tweak some settings at the beginning of the build script. You
might want to modify the paths of the toolchains if yours differ. It's
also possible to disable UPX compression and source distribution for
example.

If you're not using an IDE and want to run the build script from command
line manually each time you make a change, you can set CLEAN_BUILD to 0
so it will only compile what's needed. When modifying the resources you
should build from scratch, since CMake isn't able to track changes of
those files (e.g. changelog.txt, licenses.txt).

You can put your custom build script under "build_all.local.sh" if you
don't want git to track the changes you made to the build settings.

## Debug Build

To get proper stackstraces, you need to build with the DEBUG setting
enabled. This will set the CMake build type to debug and the script
will try to make use of cv2pdb.
Check external/cv2pdb/README.md for details on how to set it up.
It is required to use cv2pdb to generate PDB output, because at time of
writing, MinGW still didn't support Microsoft's proprietary file format.

## API

SpiceTools is providing a TCP/JSON based API. You can enable it with the
`-api [PORT]` option. It's recommended to also set a password with
`-apipass [PASS]`.

The protocol is meant to be simple, easy to use and with few overhead.
To connect to the API, just open a TCP connection to the host computer
at the specified port.
To make a request you write the UTF-8 encoded JSON contents to the
socket. To mark the end of the request, you need to terminate the JSON
string with `0x00`. The server returns its answer on the same way,
UTF-8 encoded JSON terminated with NULL.

To save space, by default the JSONs are without whitespace. To enable
pretty printing, use `-apipretty`.

If you want to test the API server without running a game, you can
run it headless via `-apidebug`.

If a password is specified, both request and response are encrypted
using RC4 with the key being the password encoded in UTF-8.
While only providing weak security when the password is static,
it's simple to implement and requires no authentication protocol.
The password is also able to change dynamically, see
`control.session_refresh()` for details.

If you don't want to worry about all the details, you can just use one
of the supplied libraries (check /api/resources). There's also a little
remote control GUI available for python.

### WebSocket
SpiceTools opens a WebSocket server on the specified port plus one.
That means if the API is on 1337, the WebSocket server will be on 1338.
The protocol is the very same as for the normal API, but instead of
directly sending the data over TCP you need to send binary datapackets.
The included dart spiceapi library also has a WebSocket implementation.

### Example Call
This example inserts the a card into P1's reader slot.
If you need more examples, you can check the example code.

#### Request
```JSON
{
  "id": 1,
  "module": "card",
  "function": "insert",
  "params": [0, "E004010000000000"]
}
```
#### Response
```JSON
{
  "id": 1,
  "errors": [],
  "data": []
}
```

### Modules

For the sake of simplifying this documentation, I will describe functions
as if they were called via a normal programming language. That means,
besides `module`/`function` needing to be set accordingly, the `params` field
must contain all the parameters described below. Some of them are optional.
The returning data will always be in the `data` field of the response.

Errors will be reported as strings in the `errors` field. Sometimes, you can
receive more than one error. The error messages may be changed/improved in the
future, so you shouldn't rely on them. If the error list is empty, the function
executed successfully.

The `id` of the response will always match the `id` of the request. The API
server itself doesn't expect any logic of your ID choices, however you can use
them to make sure your answers aren't arriving out of order. Currently it
doesn't matter since the TCP protocol doesn't allow for out of order data,
however this may change when/if support for UDP is being introduced. The only
restriction is that the ID has to be a valid 64-bit unsigned integer.

#### Card
- insert(index: uint, card_id: hex)
  - inserts a card which gets read by the emulated card readers for the game
  - index has to be either 0 (for P1) or 1 (for P2)
  - card_id has to be a valid 16-character hex string

#### Coin
- get()
  - returns the amount of unprocessed coins in queue
  - not equal to the amount of coins shown ingame since the game may drain it
- set(amount: int)
  - sets the amount of coins in queue for the game to process
- insert(amount: int)
  - adds the amount to the coins in queue
  - amount is optional and defaults to 1
- blocker_get()
  - returns the current coin blocker state (false: open, true: closed)

#### Info
- avs()
  - returns a dict including the AVS model, dest, spec, rev and ext
- launcher()
  - returns a dict including the version, compile date/time, system time and
    the arguments used to start the process
- memory()
  - returns a dict including total, total used and bytes used by the process
    for both physical and virtual RAM

#### Keypads
For all functions in this module, the keypad parameter must be
either 0 (for P1) or 1 (for P2). Accepted keypad characters are "0" to "9" for
the single digit numbers, "A" for the double zero and "D" for the decimal key.
- write(keypad: uint, input: str)
  - writes all characters in input sequentially to the keypad
  - this should be used for things like PIN input
- set(keypad: uint, key: char, ...)
  - clears all overrides, then applies each given key as override
  - if a key is overridden, it shows up as pressed in-game
- get(keypad: uint)
  - returns all pressed keys of the specified keypad

#### Analogs/Buttons/Lights
All of those three modules have equally named methods for you to call.
- read()
  - returns an array of state objects containing name, state and a bool
  - the bool indicates if the object is active (e.g. the button was overridden)
- write([name: str, state: float], ...)
  - applies the states as an override value
  - the device binding via the config will be ignored while an override is set
  - if an override value is set, the object will be marked as active
- write_reset([name: str], ...)
  - removes the override value from the objects specified by name
  - if no names were passed, all overrides will be removed

#### Touch
- read()
  - returns an array of state objects containing id, x and y
- write([id: uint, x: int, y: int], ...)
  - adds the given touch points having an unknown ID
  - overrides old touch points when the ID matches
- write_reset(id: uint, ...)
  - removes the touch points matching one of the given IDs

#### Control
This module only functions if a password is being used to communicate,
with the single exception of session_refresh().
- raise(signal: str)
  - raises a signal to the current process
  - signal is a string and must be one of the following values:
    "SIGABRT", "SIGFPE", "SIGILL", "SIGINT", "SIGSEGV", "SIGTERM"
  - the signal will be raised while the message is being processed
- exit(code: int)
  - code is optional and defaults to 0
  - the process will end while the message is being processed
- restart()
  - spawns a new instance with the same arguments as passed to the main executable
- session_refresh()
  - generates a new secure password and applies it after the response
  - can be used to increase the security by using the password as some kind of
    session token
  - recommended to call directly after connecting and once in a while for
    persistent connections
- shutdown()
  - tries to force shutdown the computer
- reboot()
  - tries to force reboot the computer

#### Memory
This module only functions if a password is being used to communicate.
All offsets are specified in file offsets of the corresponding `dll_name`,
which also means that your hex edits are applicable directly.
- write(dll_name: str, data: hex, offset: uint)
  - writes the bytes in data to the specified offset
- read(dll_name: str, offset: uint, size: uint)
  - reads bytes starting from offset and returns it
- signature(dll_name: str, signature: hex, replacement: hex, offset: uint,
            usage: uint)
  - tries to find the signature in the module's memory
  - unknown bytes are masked out via "??" (e.g. "75??90????74")
  - masking out bytes works for both the signature and the replacement
  - offset is the byte difference between the offset of the found signature and
    the offset where the replacement data gets written to
  - usage is the number of the result being used for replacement, starting at 0
  - returns the offset where the replacement data gets written to
  - the replacement string can be empty, so you can only get the address of the
    signature while not actually replacing anything

#### IIDX
- ticker_get()
  - returns a string of the characters displayed on the 16 segment display
- ticker_set(text: str)
  - sets the contents of the 16 segment display and disables writes from game
- ticker_reset()
  - re-enables writes from game

#### LCD
- info()
  - returns information about the serial LCD controller some games use

#### Resize
- image_resize_enable(enable: bool)
  - enables or disables image resize state
- image_resize_set_scene(scene: int)
  - sets the active scene for image resize state; set to 0 to disable resize

## License
Unless otherwise noted, all files are licensed under the GPLv3.
See the LICENSE file for the full license text.
Files not originating from this project should be placed in "external",
they each have their own license.

###### What that means for you
- If you want to distribute the binaries, you'll have to include the
  sources, otherwise you're violating the GPL.
- If you want to merge some of this project's code into similar
  software, that software has to be put under the GPL as well.
  You'll probably have to modify a lot anyways, so you can just use the
  source as some kind of documentation.
