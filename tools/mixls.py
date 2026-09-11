#!/usr/bin/env python3
"""List or search Tiberian Dawn MIX archives.

    tools/mixls.py data/pkg/*.mix                 # entry counts per archive
    tools/mixls.py data/pkg/*.mix -f 12GREEN.FNT  # which archives hold these names

TD MIX format: uint16 count, uint32 body size, then `count` entries of
{uint32 id, uint32 offset, uint32 size} sorted by id, then the body. The id is
Westwood's Calculate_CRC over the upper-cased file name (CRC.ASM): the name,
zero-padded to a multiple of 4 bytes, taken as little-endian dwords;
id = rotl(id, 1) + dword for each.
"""
import argparse, struct, sys
from pathlib import Path


def mix_id(name):
    data = name.upper().encode("ascii")
    data += b"\0" * (-len(data) % 4)
    crc = 0
    for (dword,) in struct.iter_unpack("<I", data):
        crc = (((crc << 1) | (crc >> 31)) + dword) & 0xFFFFFFFF
    return crc


def read_index(path):
    with open(path, "rb") as f:
        count, size = struct.unpack("<HI", f.read(6))
        entries = [struct.unpack("<III", f.read(12)) for _ in range(count)]
    return {e[0]: (e[1], e[2]) for e in entries}, size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mixes", nargs="+")
    ap.add_argument("-f", "--find", nargs="*", default=[])
    a = ap.parse_args()
    for m in a.mixes:
        idx, size = read_index(m)
        line = f"{Path(m).name:14s} {len(idx):5d} entries, body {size} bytes"
        hits = [n for n in a.find if mix_id(n) in idx]
        if a.find:
            line += "  has: " + (" ".join(hits) if hits else "-")
        print(line)


if __name__ == "__main__":
    main()
