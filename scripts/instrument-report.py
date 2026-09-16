#!/usr/bin/env python3
import csv
import html
import math
import sys
from pathlib import Path

source = Path(sys.argv[1] if len(sys.argv) > 1 else "instrument-turns.csv")
target = Path(sys.argv[2] if len(sys.argv) > 2 else "instrument/report.html")
run_at = sys.argv[3] if len(sys.argv) > 3 else "unknown local time"
hello = sys.argv[4] if len(sys.argv) > 4 else "unknown build"
rows = list(csv.DictReader(source.open()))
if not rows:
    raise SystemExit(f"No instrument turns in {source}")


def blocks(milliseconds):
    return max(1, math.ceil(float(milliseconds) / 10.0))


def block_bar(row, opening=False):
    used = float(row["elapsed_ms"])
    count = blocks(used)
    title = f'{used:.3f} ms ({count} × 10 ms blocks)'
    if opening:
        cells = "".join('<span class="hblock"></span>' for _ in range(count))
        return f'<div class="opening-bar" title="{title}">{cells}</div>'
    cells = "".join('<span class="vblock"></span>' for _ in range(min(count, 12)))
    return (
        f'<div class="turn"><div class="bar" title="{title}">{cells}</div>'
        f'<div class="move-label">{html.escape(row["move"])}</div></div>'
    )


opening = rows[0]
normal = rows[1:]
headers = [
    ("move", "Move"),
    ("elapsed_ms", "Time used (ms)"),
    ("legal_moves", "Legal moves"),
    ("possibilities_evaluated", "Possibilities evaluated"),
    ("deepest_completed_ply", "Deepest completed ply"),
    ("winning_row", "Winning row"),
    ("winning_col", "Winning col"),
    ("winning_score", "Selected backed-up score"),
    ("scored_positions", "Scored positions"),
    ("cache_lookups", "Cache lookups"),
    ("cache_hits", "Cache hits"),
    ("search", "Search"),
]

table_rows = []
for row in rows:
    cells = "".join(f"<td>{html.escape(row[key])}</td>" for key, _ in headers)
    table_rows.append(f"<tr>{cells}</tr>")

document = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Ultimate Tic-Tac-Toe instrument report</title>
<style>
body {{ font: 14px system-ui, sans-serif; margin: 24px; color: #17202a; }}
h1,h2 {{ margin-bottom: .4rem; }} .note {{ color:#566573; }}
.opening-bar {{ display:flex; flex-wrap:wrap; gap:2px; max-width:900px; }}
.hblock {{ width:7px; height:14px; background:#2471a3; border-radius:1px; }}
.chart {{ height:150px; display:flex; align-items:flex-end; gap:5px; padding:10px;
  border-left:1px solid #777; border-bottom:1px solid #777;
  background:repeating-linear-gradient(to top,#fff 0,#fff 9px,#d5d8dc 10px); }}
.turn {{ height:140px; min-width:20px; display:flex; flex-direction:column; justify-content:flex-end; align-items:center; }}
.bar {{ display:flex; flex-direction:column-reverse; width:14px; }}
.vblock {{ box-sizing:border-box; height:10px; background:#2e86c1; border-top:1px solid #aed6f1; }}
.move-label {{ margin-top:4px; font-size:11px; }}
table {{ border-collapse:collapse; margin-top:14px; }} th,td {{ border:1px solid #ccd1d1; padding:5px 8px; text-align:right; }}
th {{ background:#eaf2f8; position:sticky; top:0; }} td:last-child,th:last-child {{ text-align:left; }}
</style></head><body>
<h1>Ultimate Tic-Tac-Toe: one-game instrument report</h1>
<p><strong>Run:</strong> {html.escape(run_at)}<br>
<strong>Bot:</strong> {html.escape(hello)}</p>
<p class="note">Each coloured block represents 10 ms, rounded up. The bot targets 900 ms on its opening move and 90 ms thereafter, leaving arena safety margin.</p>
<p class="note">Positional scores use 10000 × (ours − theirs) / (ours + theirs + 2): 0 is neutral, 1000 is a 10% normalized advantage, and ±60000 is a proved terminal result.</p>
<h2>Opening move — {float(opening['elapsed_ms']):.3f} ms</h2>
{block_bar(opening, True)}
<h2>Regular turns — 10 ms vertical blocks</h2>
<div class="chart">{''.join(block_bar(row) for row in normal)}</div>
<h2>Move search details</h2>
<table><thead><tr>{''.join(f'<th>{label}</th>' for _, label in headers)}</tr></thead>
<tbody>{''.join(table_rows)}</tbody></table>
</body></html>"""
target.write_text(document)
print(target)
