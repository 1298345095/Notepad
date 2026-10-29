#!/usr/bin/env python3
"""Create deterministic, valid UTF-8 input for editor loading benchmarks."""

import argparse
from pathlib import Path


def positive_int(value: str) -> int:
    number = int(value)
    if number < 1:
        raise argparse.ArgumentTypeError("must be at least 1")
    return number


def generate(path: Path, size_mib: int, kind: str) -> int:
    size_bytes = size_mib * 1024 * 1024
    record = "2026-01-01T00:00:00Z INFO 文本编辑器 UTF-8 fixture 0123456789 abcdef\n".encode("utf-8")
    if kind == "long-line":
        record = record.replace(b"\n", b" ")

    # Write complete UTF-8 records, then ASCII padding. Truncating an arbitrary
    # byte buffer at the target size could otherwise split a Chinese character.
    repeats, padding = divmod(size_bytes - 1, len(record))
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        block_count = max(1, (1024 * 1024) // len(record))
        block = record * block_count
        while repeats >= block_count:
            stream.write(block)
            repeats -= block_count
        stream.write(record * repeats)
        stream.write(b"x" * padding)
        stream.write(b"\n")
    return size_bytes


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="output file (replaced if it exists)")
    parser.add_argument("--size-mib", type=positive_int, default=10)
    parser.add_argument("--kind", choices=("lines", "long-line"), default="lines")
    args = parser.parse_args()
    size_bytes = generate(args.output, args.size_mib, args.kind)
    print(f"{args.output}: {size_bytes} bytes; {args.kind}; UTF-8")


if __name__ == "__main__":
    main()
