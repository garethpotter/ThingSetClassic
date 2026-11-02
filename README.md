# ThingSet Monitor for classic Mac OS

A small 68k application for watching [ThingSet](https://thingset.io/) devices, built
on [ThingSet++](../thingsetplusplus).

## Monitor

The monitor window shows the latest values from reports broadcast over UDP (port 9002),
one row per sender and ID:

```text
Sender          Subset  ID           Value                  Updated
10.0.0.5        0x1     0x300        24.5                   11:42:07
10.0.0.5        0x1     0x301        true                   11:42:07
192.168.1.23    0x2     0x700        false                  11:42:05
```

Changed values are shown in bold for a moment. Rows can be selected (shift-click
for a range) and copied as tab-separated text; with nothing selected, Copy takes
the whole table. The status bar shows counts and the latest problem, such as an
undecodable report.

To save effort on a busy network, the monitor only listens while it is the active
window; it pauses while another window is in front or the application is in the
background.

## Client

!(docs/value_viewer.png)

File > Connect To... (or double-clicking a row in the monitor) opens a window
browsing one device over TCP (port 9001):

```text
Name                      Value               Type        Access
v 10.0.0.9
  > Battery                                   group       r
    Interval              1000                u32         rw
    NodeID                "A1B2C3D4E5F60708"  string      r
```

The schema is discovered a group at a time, as groups are expanded, using
`FETCH <group> null` for child IDs and `FETCH _Metadata [ids]` for names, types
and access, falling back to `FETCH <path> null` for names (as ThingSet.Net does).
Values of expanded groups are read with `GET <group>` when a group is loaded or
expanded; if a group is too large for the device's response buffer, its values are
read one at a time. The view doesn't update itself, which is too slow on a 68k Mac:
click Refresh (or choose File > Refresh, Command-R) to re-read everything expanded.
Values that changed are shown in bold for a moment.

Records and record arrays expand too: a record array lists its elements (`[0]`,
`[1]`, ...), each of which expands to its members, including nested record arrays.
Their values come from the enclosing group's `GET`; where a device gives only an
array's length (as `GET` on the array itself does), elements are read by path,
e.g. `GET Modules/1`.

Requests are encoded and responses decoded with ThingSet++, but the connection is
driven from the event loop rather than through `ThingSetClient`, whose blocking
reads would freeze the Mac.

Requires a 68030 or later or any Power Mac, System 7.1+ and Open Transport with
TCP/IP. The fat build runs natively on both.

## Building

Needs [Retro68](https://github.com/autc04/Retro68), built with both its 68k and
PowerPC toolchains, and a ThingSet++ checkout (default `../thingsetplusplus`,
override with `-DTHINGSETPP_DIR=...`).

To build a fat application, which runs natively on both 68k and Power Macs:

```sh
cmake -S fat -B build-fat -DRETRO68_TOOLCHAIN=path/to/Retro68-build/toolchain
cmake --build build-fat
```

This produces `build-fat/ThingSetMonitor.bin` (MacBinary) and
`build-fat/ThingSetMonitor.dsk` (an HFS disk image containing the application).
The 68k code is in `CODE` resources as usual; the PowerPC code is a fragment in
the data fork, found through a `cfrg` resource.

To build for one architecture only, build the top-level project with the
corresponding toolchain file:

```sh
# 68k
cmake -S . -B build \
    -DCMAKE_TOOLCHAIN_FILE=path/to/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
# PowerPC
cmake -S . -B build-ppc \
    -DCMAKE_TOOLCHAIN_FILE=path/to/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
```

Unless a build type is given, 68k code is built with `-Os` and PowerPC code with
`-O2` (`-Os` currently fails to link against Retro68's PowerPC libstdc++).

### Icon

The application icon is pixel art after the ThingSet logo, kept as text in
`tools/icon.txt`. After editing it, regenerate `src/Icon.r` with
`python3 tools/make-icon.py`. The build runs `tools/set-bundle-bit.py` on its
outputs to set the Finder's "has bundle" flag, which Rez leaves clear and without
which the Finder ignores the icon. If an old copy of the application has already
been seen by the Finder, rebuild the desktop (hold Command-Option at startup) to
see the new icon.

## Tests

The report handling and the client can be tested on the host against stub Open
Transport headers. The client test talks to ThingSet++'s own server, with responses
split into small pieces as TCP may deliver them (set `TRACE=1` to see the requests):

```sh
cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
```

## Limitations

- Enhanced reports (`0x1E`, prefixed with the sender's EUI-64) are noted in the
  status bar but not decoded: ThingSet++'s UDP decoder only expects two top-level elements.
- The monitor shows values by numeric ID; it doesn't use a device's schema for names.
- Host names are looked up synchronously, so the Mac pauses briefly while connecting by name.
- The client is read-only: values can't be changed and functions can't be called.
- Record members are shown by ID (e.g. `0x201`) when the device doesn't name them:
  ThingSet++ answers neither `FETCH` on a record nor `_Metadata` for its members.
- The table holds up to 2000 rows; further new items are ignored until it is cleared.
