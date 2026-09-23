#!/usr/bin/env python3
"""Rewrite the binary EData log written by ai_negamax as a CSV.

The EData layout is read straight out of src/bots/ai_negamax.c, so adding or
resizing a field costs nothing here.  Array fields expand to one column per
element (name_0, name_1, ...); scalar fields keep their name.  A field carrying
a trailing /* EDATA_DELTA */ comment is written as its change from the previous
record rather than its cumulative value (the record before the first is taken to
be all zeroes).  Unmarked fields keep their cumulative value.  Each record's own
leading size field is used as the record stride, and a partial trailing record
(the bot is still writing) is ignored.

Input is $NEGAMAX_RESULTS unless a path is given; output goes to stdout unless
a second path is given.

    ./scripts/edata-csv.py > work/edata.csv
    ./scripts/edata-csv.py work/edata.bin work/edata.csv
"""
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "src" / "bots" / "ai_negamax.c"

# C type -> struct format character.  Only the scalar types actually in use;
# an unknown type stops loudly instead of guessing.
CTYPES = {
    "u8": "B", "i8": "b",
    "u16": "H", "i16": "h",
    "u32": "I", "i32": "i",
    "u64": "Q", "i64": "q",
    "f32": "f", "f64": "d", "float": "f", "double": "d",
}

MEMBER = re.compile(r"^\s*([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*(?:\[\s*(\d+)\s*\])?\s*;")


def parse_fields(source):
    """Return ([(csv_names, format, offset, delta)], struct_size) for EData."""
    match = re.search(r"typedef\s+struct\s*\{([^}]*)\}\s*EData\s*;", source)
    if not match:
        sys.exit("edata-csv: no EData struct found in %s" % SOURCE)

    fields, offset, align = [], 0, 1
    for line in match.group(1).splitlines():
        m = MEMBER.match(line)
        if not m:
            continue
        ctype, name, count = m.group(1), m.group(2), m.group(3)
        fmt = CTYPES.get(ctype)
        if fmt is None:
            sys.exit("edata-csv: unsupported C type %r for %s" % (ctype, name))
        size = struct.calcsize(fmt)
        count = int(count) if count else 1
        align = max(align, size)
        offset = (offset + size - 1) // size * size  # natural alignment
        names = [name] if count == 1 else ["%s_%d" % (name, i) for i in range(count)]
        delta = "EDATA_DELTA" in line[m.end():]
        fields.append((names, fmt * count, offset, delta))
        offset += size * count
    offset = (offset + align - 1) // align * align  # trailing struct padding
    return fields, offset


def main():
    args = sys.argv[1:]
    path = args[0] if args else os.environ.get("NEGAMAX_RESULTS")
    if not path:
        sys.exit("edata-csv: set NEGAMAX_RESULTS or pass the data file as an argument")

    fields, struct_size = parse_fields(SOURCE.read_text())
    out = open(args[1], "w", newline="") if len(args) > 1 else sys.stdout
    out.write(",".join(n for names, _, _, _ in fields for n in names) + "\n")

    data = Path(path).read_bytes()
    if not data:
        return
    rec_size = struct.unpack_from("<I", data)[0] or struct_size
    if rec_size != struct_size:
        print("edata-csv: warning: record size %d != current struct %d"
              % (rec_size, struct_size), file=sys.stderr)

    # The record before the first is all zeroes, so a delta field's first row
    # equals its raw value.
    previous = [tuple(0 for _ in names) for names, _, _, _ in fields]
    for base in range(0, len(data) - rec_size + 1, rec_size):
        row = []
        for i, (_, fmt, offset, delta) in enumerate(fields):
            raw = struct.unpack_from("<" + fmt, data, base + offset)
            if delta:
                row.extend(v - p for v, p in zip(raw, previous[i]))
            else:
                row.extend(raw)
            previous[i] = raw
        out.write(",".join(str(v) for v in row) + "\n")
    if len(data) % rec_size:
        print("edata-csv: ignored %d trailing bytes" % (len(data) % rec_size),
              file=sys.stderr)


if __name__ == "__main__":
    main()
