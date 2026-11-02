# ThingSetClient Architecture

Some architectural details (to be precise, Claude waffle that doesn't belong in a README).

## Value Viewer

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

## Build

When building a fat binary, the 68k code is in `CODE` resources as usual; the PowerPC code is a fragment in
the data fork, found through a `cfrg` resource.

## Icon

The application icon is pixel art after the ThingSet logo, kept as text in
`tools/icon.txt`. After editing it, regenerate `src/Icon.r` with
`python3 tools/make-icon.py`. The build runs `tools/set-bundle-bit.py` on its
outputs to set the Finder's "has bundle" flag, which Rez leaves clear and without
which the Finder ignores the icon. If an old copy of the application has already
been seen by the Finder, rebuild the desktop (hold Command-Option at startup) to
see the new icon.
