#!/usr/bin/env python3
import csv
import html
import math
import sys
from datetime import datetime
from pathlib import Path

source = Path(sys.argv[1] if len(sys.argv) > 1 else "instrument-turns.csv")
target = Path(sys.argv[2] if len(sys.argv) > 2 else "instrument/report.html")
run_at = sys.argv[3] if len(sys.argv) > 3 else "unknown local time"
hello = sys.argv[4] if len(sys.argv) > 4 else "unknown build"
rows = list(csv.DictReader(source.open()))
score_source = Path(sys.argv[5]) if len(sys.argv) > 5 else None
scores = list(csv.DictReader(score_source.open())) if score_source else []
moves_source = Path(sys.argv[6]) if len(sys.argv) > 6 else None
game_moves = list(csv.DictReader(moves_source.open())) if moves_source else []
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

score_rows = []
for score in scores:
    score_rows.append(
        "<tr>" + "".join(f"<td>{html.escape(score[key])}</td>"
                          for key in ("ply", "depth", "row", "col", "score")) + "</tr>"
    )
score_details = (
    f'<details><summary>All completed root scores ({len(scores)})</summary>'
    '<table><thead><tr><th>Game ply</th><th>Search depth</th><th>Row</th>'
    '<th>Col</th><th>Score</th></tr></thead><tbody>'
    + "".join(score_rows) + "</tbody></table></details>"
    if score_source else ""
)


def board_cards():
    if not moves_source:
        return ""
    ordered = sorted(game_moves, key=lambda move: int(move["ply"]))
    our_moves = [move for move in ordered if move["player"] == "0"]
    if len(our_moves) != len(rows):
        raise SystemExit("Instrument turns do not match p0 moves in the move trace")
    cards = []
    winning_lines = ((0, 1, 2), (3, 4, 5), (6, 7, 8), (0, 3, 6),
                     (1, 4, 7), (2, 5, 8), (0, 4, 8), (2, 4, 6))
    for index, (turn, chosen) in enumerate(zip(rows, our_moves)):
        ply = int(chosen["ply"])
        occupied = {}
        for previous in ordered:
            if int(previous["ply"]) >= ply:
                break
            square = (int(previous["row"]), int(previous["col"]))
            if square in occupied:
                raise SystemExit(f"Duplicate played square before ply {ply}: {square}")
            occupied[square] = "X" if previous["player"] == "0" else "O"
        selected = (int(chosen["row"]), int(chosen["col"]))
        last_op = (int(ordered[ply - 1]["row"]), int(ordered[ply - 1]["col"])) if ply else None
        if selected in occupied:
            raise SystemExit(f"Selected occupied square at ply {ply}: {selected}")
        latest = {}
        for event in scores:
            if int(event["ply"]) == ply:
                latest[(int(event["row"]), int(event["col"]))] = event
        mini_boards = []
        for outer_row in range(3):
            for outer_col in range(3):
                mini = [occupied.get((outer_row * 3 + inner_row,
                                      outer_col * 3 + inner_col), ".")
                        for inner_row in range(3) for inner_col in range(3)]
                owner = next((player for player in ("X", "O")
                              if any(all(mini[cell] == player for cell in line)
                                     for line in winning_lines)), None)
                status = (f"{owner} WON" if owner else "DRAW" if "." not in mini else "")
                status_class = ("closed-x" if owner == "X" else
                                "closed-o" if owner == "O" else
                                "closed-draw" if status else "open")
                cells = []
                for inner_row in range(3):
                    for inner_col in range(3):
                        row = outer_row * 3 + inner_row
                        col = outer_col * 3 + inner_col
                        square = (row, col)
                        classes = ["square"]
                        if square == selected:
                            classes.append("selected")
                        if square == last_op:
                            classes.append("last-op")
                        if square in occupied:
                            classes.append("ours" if occupied[square] == "X" else "theirs")
                            content = occupied[square]
                            title = f"row {row}, col {col}: played {content}"
                        elif square in latest:
                            event = latest[square]
                            value = int(event["score"])
                            classes.append("positive" if value > 0 else "negative" if value < 0 else "neutral")
                            content = f"{value:+d}" if value else "0"
                            title = f"row {row}, col {col}: score {value} at depth {event['depth']}"
                        else:
                            content = "·"
                            title = f"row {row}, col {col}: not evaluated"
                        content_html = html.escape(content)
                        if square == last_op:
                            content_html += '<span class="last-tag">LAST</span>'
                        cells.append(
                            f'<div class="{" ".join(classes)}" title="{html.escape(title)}">'
                            f'{content_html}</div>'
                        )
                label = f'<span class="mini-status">{status}</span>' if status else ""
                mini_boards.append(
                    f'<div class="mini {status_class}" title="Small board '
                    f'{outer_row},{outer_col}: {status or "open"}">'
                    f'{label}<div class="mini-grid">{"".join(cells)}</div></div>'
                )
        last_text = (f'Opponent just played <strong>row {last_op[0]}, col {last_op[1]}</strong> · '
                     if last_op else 'Opening · ')
        cards.append(
            f'<section class="board-card"><h3>Our turn {index + 1} · game ply {ply + 1}</h3>'
            f'<p>{last_text}played <strong>row {selected[0]}, col {selected[1]}</strong> · '
            f'{html.escape(turn["elapsed_ms"])} ms · '
            f'{len(latest)} scored choices · selected score {html.escape(turn["winning_score"])}'
            f'</p><div class="board">{"".join(mini_boards)}</div></section>'
        )
    return '<div class="boards">' + "\n".join(cards) + '</div>'


boards_html = board_cards()

document = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Ultimate Tic-Tac-Toe instrument report</title>
<style>
body {{ font: 14px system-ui, sans-serif; margin: 24px; color: #17202a; }}
h1,h2 {{ margin-bottom: .4rem; }} .note {{ color:#566573; }}
.boards {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(470px,1fr)); gap:18px; margin:18px 0 30px; }}
.board-card {{ border:1px solid #cbd5e1; border-radius:10px; padding:14px; background:#f8fafc; }}
.board-card h3 {{ margin:0; }} .board-card p {{ margin:7px 0 12px; color:#475569; }}
.board {{ display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); max-width:540px; border:2px solid #334155; }}
.mini {{ position:relative; border:2px solid #334155; min-width:0; }}
.mini-grid {{ display:grid; grid-template-columns:repeat(3,minmax(0,1fr)); }}
.mini-status {{ position:absolute; top:4px; left:4px; z-index:3; border-radius:4px;
  padding:2px 5px; font-size:11px; font-weight:900; letter-spacing:.04em;
  background:#1e293b; color:white; box-shadow:0 1px 3px #475569; }}
.closed-x {{ border-color:#1d4ed8; }} .closed-x .mini-status {{ background:#1d4ed8; }}
.closed-o {{ border-color:#b91c1c; }} .closed-o .mini-status {{ background:#b91c1c; }}
.closed-draw {{ border-color:#64748b; }} .closed-draw .mini-status {{ background:#64748b; }}
.square {{ position:relative; box-sizing:border-box; min-width:0; aspect-ratio:1;
  display:flex; align-items:center; justify-content:center;
  border-right:1px solid #cbd5e1; border-bottom:1px solid #cbd5e1;
  font-size:clamp(9px,1vw,13px); font-weight:650; }}
.mini.closed-x .square {{ background:#dbeafe; }}
.mini.closed-o .square {{ background:#fee2e2; }}
.mini.closed-draw .square {{ background:#e2e8f0; }}
.ours {{ background:#dbeafe; color:#1d4ed8; font-size:22px; }}
.theirs {{ background:#fee2e2; color:#b91c1c; font-size:22px; }}
.positive {{ background:#dcfce7; color:#166534; }} .negative {{ background:#fee2e2; color:#991b1b; }}
.neutral {{ background:#f1f5f9; }}
.selected {{ outline:3px solid #eab308; outline-offset:-4px; background:#fef08a; color:#17202a; z-index:1; }}
.last-op {{ outline:4px solid #7c3aed; outline-offset:-5px; z-index:2; font-weight:900; }}
.last-tag {{ position:absolute; right:2px; bottom:1px; border-radius:2px;
  padding:0 2px; background:#7c3aed; color:white; font-size:8px; line-height:12px; }}
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
<strong>Bot:</strong> {html.escape(hello)}<br>
<strong>Generated:</strong> {html.escape(datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z'))}</p>
<p class="note">Each coloured block represents 10 ms, rounded up. This instrument run requests a 75 ms search budget to allow time for telemetry; normal local runs use 90 ms.</p>
<p class="note">Positional scores use 10000 × (ours − theirs) / (ours + theirs + 2): 0 is neutral, 1000 is a 10% normalized advantage, and ±60000 is a proved terminal result.</p>
<h2>Game progression — one board per decision</h2>
<p class="note">X is our earlier move, O is the opponent’s. The purple ring and LAST label mark the opponent’s immediately preceding move; yellow marks our choice. Strong blue/red/grey borders and X WON/O WON/DRAW badges mark closed small boards. Numbers are each candidate’s latest completed score; green favours us, red favours the opponent. Hover for coordinates and depth.</p>
{boards_html}
<h2>Opening move — {float(opening['elapsed_ms']):.3f} ms</h2>
{block_bar(opening, True)}
<h2>Regular turns — 10 ms vertical blocks</h2>
<div class="chart">{''.join(block_bar(row) for row in normal)}</div>
<h2>Move search details</h2>
<table><thead><tr>{''.join(f'<th>{label}</th>' for _, label in headers)}</tr></thead>
<tbody>{''.join(table_rows)}</tbody></table>
{score_details}
</body></html>"""
target.write_text(document)
print(target)
