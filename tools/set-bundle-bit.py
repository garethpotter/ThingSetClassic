#!/usr/bin/env python3
"""Sets the Finder's "has bundle" flag on Retro68's application outputs.

Rez always leaves the Finder flags clear, but the Finder only reads an
application's BNDL resource, and so shows its icon, when the flag is set.

    python3 tools/set-bundle-bit.py App.bin App.dsk App.APPL %App.ad

Handles MacBinary (.bin), HFS disk images (.dsk), macOS files with Finder info
in an extended attribute (.APPL) and AppleDouble headers (%*.ad). Files that
don't exist are skipped, so the same list suits every build.
"""
import os
import struct
import subprocess
import sys

HAS_BUNDLE = 0x2000


def crc16_xmodem(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = (crc << 1 ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc


def patch_macbinary(path):
    data = bytearray(open(path, 'rb').read())
    if data[0] != 0 or not 1 <= data[1] <= 63 or crc16_xmodem(data[:124]) != struct.unpack('>H', data[124:126])[0]:
        raise SystemExit(f'{path}: not a MacBinary II file')
    data[73] |= HAS_BUNDLE >> 8
    data[124:126] = struct.pack('>H', crc16_xmodem(data[:124]))
    open(path, 'wb').write(data)


def patch_hfs_image(path):
    """Sets the flag on every application in the volume's catalog."""
    data = bytearray(open(path, 'rb').read())
    mdb = 1024
    if data[mdb:mdb + 2] != b'BD':
        raise SystemExit(f'{path}: not an HFS volume')
    block_size, = struct.unpack('>I', data[mdb + 20:mdb + 24])
    first_block, = struct.unpack('>H', data[mdb + 28:mdb + 30])
    catalog_size, = struct.unpack('>I', data[mdb + 146:mdb + 150])

    # the catalog file's extents: (start, count) in allocation blocks
    catalog = bytearray()
    for i in range(3):
        start, count = struct.unpack('>HH', data[mdb + 150 + 4 * i:mdb + 154 + 4 * i])
        offset = first_block * 512 + start * block_size
        catalog += data[offset:offset + count * block_size]
    if len(catalog) < catalog_size:
        raise SystemExit(f'{path}: catalog has overflow extents, which this script does not handle')

    def catalog_offset(position):
        # map a position in the catalog file back to the disk image
        for i in range(3):
            start, count = struct.unpack('>HH', data[mdb + 150 + 4 * i:mdb + 154 + 4 * i])
            if position < count * block_size:
                return first_block * 512 + start * block_size + position
            position -= count * block_size
        raise ValueError(position)

    node_size, = struct.unpack('>H', catalog[14 + 18:14 + 20])
    node, = struct.unpack('>I', catalog[14 + 10:14 + 14])  # first leaf node
    patched = 0
    while node:
        base = node * node_size
        forward, = struct.unpack('>I', catalog[base:base + 4])
        records, = struct.unpack('>H', catalog[base + 10:base + 12])
        for i in range(records):
            record, = struct.unpack('>H', catalog[base + node_size - 2 * (i + 1):base + node_size - 2 * i])
            key_length = catalog[base + record]
            body = base + record + 1 + key_length
            body += body % 2
            # a file record whose type is APPL; its FInfo.fdFlags follow the type and creator
            if catalog[body] == 2 and catalog[body + 4:body + 8] == b'APPL':
                flags_at = catalog_offset(body + 12)
                flags, = struct.unpack('>H', data[flags_at:flags_at + 2])
                data[flags_at:flags_at + 2] = struct.pack('>H', flags | HAS_BUNDLE)
                patched += 1
        node = forward
    if not patched:
        raise SystemExit(f'{path}: no application found in the catalog')
    open(path, 'wb').write(data)


def patch_xattr(path):
    try:
        info = subprocess.run(['xattr', '-px', 'com.apple.FinderInfo', path], capture_output=True, text=True,
                              check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return  # not on macOS, or no Finder info to change
    finder_info = bytearray.fromhex(info.replace(' ', '').replace('\n', ''))
    finder_info[8] |= HAS_BUNDLE >> 8
    subprocess.run(['xattr', '-wx', 'com.apple.FinderInfo', finder_info.hex(), path], check=True)


def patch_appledouble(path):
    data = bytearray(open(path, 'rb').read())
    magic, _ = struct.unpack('>II', data[:8])
    if magic != 0x00051607:
        raise SystemExit(f'{path}: not an AppleDouble header file')
    entries, = struct.unpack('>H', data[24:26])
    for i in range(entries):
        entry_id, offset, length = struct.unpack('>III', data[26 + 12 * i:38 + 12 * i])
        if entry_id == 9 and length >= 10:  # Finder info
            data[offset + 8] |= HAS_BUNDLE >> 8
    open(path, 'wb').write(data)


def main():
    for path in sys.argv[1:]:
        if not os.path.exists(path):
            continue
        name = os.path.basename(path)
        if name.endswith('.bin'):
            patch_macbinary(path)
        elif name.endswith('.dsk'):
            patch_hfs_image(path)
        elif name.startswith('%') and name.endswith('.ad'):
            patch_appledouble(path)
        elif name.endswith('.APPL'):
            patch_xattr(path)


if __name__ == '__main__':
    main()
