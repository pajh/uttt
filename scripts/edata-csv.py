#!/usr/bin/env python3
"""Rewrite the binary EData log written by ai_negamax as a CSV.

The EData layout is read straight out of src/bots/ai_negamax.c, so adding or
resizing a field costs nothing here.  Array fields expand to one column per
element (name_0, name_1, ...); scalar fields keep their name.  Each record's
own leading size field is used as the record stride, and a partial trailing
record (the bot is still writing) is ignored.

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
    """Return ([(csv_names, format, offset)], struct_size) for EData."""
    match = re.search(r"typedef\s+struct\s*\{([^}]*)\}\s*EData\s*;", source)
    if not match:
        sys.exit("edata-csv: no EData struct found in %s" % SOURCE)

    fields, offset = [], 0
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
        offset = (offset + size - 1) // size * size  # natural alignment
        names = [name] if count == 1 else ["%s_%d" % (name, i) for i in range(count)]
        fields.append((names, fmt * count, offset))
        offset += size * count
    return fields, offset


def main():
    args = sys.argv[1:]
    path = args[0] if args else os.environ.get("NEGAMAX_RESULTS")
    if not path:
        sys.exit("edata-csv: set NEGAMAX_RESULTS or pass the data file as an argument")

    fields, struct_size = parse_fields(SOURCE.read_text())
    out = open(args[1], "w", newline="") if len(args) > 1 else sys.stdout
    out.write(",".join(n for names, _, _ in fields for n in names) + "\n")

    data = Path(path).read_bytes()
    if not data:
        return
    rec_size = struct.unpack_from("<I", data)[0] or struct_size
    if rec_size != struct_size:
        print("edata-csv: warning: record size %d != current struct %d"
              % (rec_size, struct_size), file=sys.stderr)

    for base in range(0, len(data) - rec_size + 1, rec_size):
        row = []
        for _, fmt, offset in fields:
            row.extend(struct.unpack_from("<" + fmt, data, base + offset))
        out.write(",".join(str(v) for v in row) + "\n")
    if len(data) % rec_size:
        print("edata-csv: ignored %d trailing bytes" % (len(data) % rec_size),
              file=sys.stderr)


if __name__ == "__main__":
    main()
