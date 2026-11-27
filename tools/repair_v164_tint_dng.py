#!/usr/bin/env python3
"""Patch ONLY the DNG metadata header of a C3M1 v0.1.64 RAW642 image.

Usage:
    python tools/repair_v164_tint_dng.py source_v164.DNG patched_v165.DNG

Does not change RAW pixel bytes. This is a diagnostic/compatibility tool for
Camera Raw; it cannot infer the original sensor AWB registers from other DNGs.
"""
from __future__ import annotations
import argparse
import hashlib
import struct
from pathlib import Path

HEADER = 512
WIDTH, HEIGHT = 642, 480
EXPECTED = HEADER + WIDTH * HEIGHT
REF_R, REF_B = 1045, 533
REF_PRODUCT = REF_R * REF_B * 1000


def parse_ifd(data: bytes) -> dict[int, tuple[int, int, int, int]]:
    if data[:4] != b'II*\x00' or struct.unpack_from('<I', data, 4)[0] != 8:
        raise ValueError('Unsupported TIFF/DNG endian or IFD offset')
    count = struct.unpack_from('<H', data, 8)[0]
    if not (1 <= count <= 30) or 10 + 12 * count + 4 > HEADER:
        raise ValueError('Invalid DNG IFD')
    tags = {}
    for i in range(count):
        off = 10 + 12 * i
        tag, kind, n, value = struct.unpack_from('<HHII', data, off)
        if tag in tags:
            raise ValueError('Duplicated DNG tag')
        tags[tag] = (kind, n, value, off)
    return tags


def edit_v164_dng(data: bytes) -> tuple[bytes, tuple[float, float], tuple[float, float]]:
    if len(data) != EXPECTED:
        raise ValueError(f'Expected v0.1.64 642x480 RAW8 DNG ({EXPECTED} bytes)')
    tags = parse_ifd(data)
    def require(tag: int, kind: int, count: int, value: int | None = None):
        got = tags.get(tag)
        if got is None or got[0:2] != (kind, count) or (value is not None and got[2] != value):
            raise ValueError(f'Tag {tag} is not the expected v0.1.64 type/value')
        return got
    require(256, 4, 1, WIDTH)
    require(257, 4, 1, HEIGHT)
    require(258, 3, 1, 8)
    require(279, 4, 1, WIDTH * HEIGHT)
    require(50778, 3, 1, 20)  # v164 D55 label
    cm = require(50721, 10, 9)
    fm = require(50964, 10, 9)
    n = require(50728, 5, 3)
    if cm[2] + 72 > HEADER or fm[2] + 72 > HEADER or n[2] + 24 > HEADER:
        raise ValueError('Invalid metadata location')
    if struct.unpack_from('<ii', data, cm[2]) != (216326, 100000) or \
       struct.unpack_from('<ii', data, fm[2]) != (55316, 100000):
        raise ValueError('Not the expected v0.1.64 experimental profile')
    neutral = [struct.unpack_from('<II', data, n[2] + 8 * i) for i in range(3)]
    if any(den != 100000 for _, den in neutral) or neutral[1][0] != 100000:
        raise ValueError('Not a v0.1.64 AsShotNeutral representation')
    rr, bb = neutral[0][0], neutral[2][0]
    if not 50000 <= rr <= 200000 or not 30000 <= bb <= 200000:
        raise ValueError('Unsupported AWB neutral')
    den_r = (rr + 5) // 10
    den_b = (bb + 5) // 10
    out_r = (REF_PRODUCT + den_b // 2) // den_b
    out_b = (REF_PRODUCT + den_r // 2) // den_r
    if not 50000 <= out_r <= 200000 or not 30000 <= out_b <= 200000:
        raise ValueError('Mirrored neutral out of range: newer firmware would fall back')
    patched = bytearray(data)
    struct.pack_into('<I', patched, n[2], out_r)
    struct.pack_into('<I', patched, n[2] + 16, out_b)
    struct.pack_into('<I', patched, tags[50778][3] + 8, 23)  # D50
    assert patched[HEADER:] == data[HEADER:]
    return bytes(patched), (rr / 100000, bb / 100000), (out_r / 100000, out_b / 100000)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('Output must differ from input; never overwrite RAW images')
    if args.output.exists():
        parser.error('Output exists; refusing overwrite')
    data = args.source.read_bytes()
    fixed, before, after = edit_v164_dng(data)
    args.output.write_bytes(fixed)
    print(f'Saved: {args.output} ({len(fixed)} bytes)')
    print(f'AsShotNeutral R/B: {before} -> {after}, G=1')
    print('CalibrationIlluminant1: 20 D55 -> 23 D50')
    print('RAW SHA-256 (unchanged):', hashlib.sha256(data[HEADER:]).hexdigest())


if __name__ == '__main__':
    main()
