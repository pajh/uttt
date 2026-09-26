#!/usr/bin/env python3
"""Render one instrumented gamerig JSON game as a board-per-move HTML report.

Usage: instrument-report.py <game.json> <report.html>

The JSON must carry a header "instrumented" field naming the instrumented
player; without it the game was not instrumented and the report refuses.
"""
import html
import json
import sys
from datetime import datetime
from pathlib import Path

source = Path(sys.argv[1] if len(sys.argv) > 1 else "game.json")
target = Path(sys.argv[2] if len(sys.argv) > 2 else "report.html")
game = json.loads(source.read_text())
header = game["header"]
if "instrumented" not in header:
    raise SystemExit(f"No instrumentation data in {source}")
player = int(header["instrumented"])
moves = sorted(game["moves"], key=lambda move: int(move["moveno"]))
result = game["result"]

WINNING_LINES = ((0, 1, 2), (3, 4, 5), (6, 7, 8), (0, 3, 6),
                 (1, 4, 7), (2, 5, 8), (0, 4, 8), (2, 4, 6))


def mini_status(mini):
    owner = next((mark for mark in ("X", "O")
                  if any(all(mini[cell] == mark for cell in line)
                         for line in WINNING_LINES)), None)
    if owner:
        return owner, f"{owner} WON", ("closed-x" if owner == "X" else "closed-o")
    if "." not in mini:
        return None, "DRAW", "closed-draw"
    return None, "", "open"


def board_cards():
    our_moves = [move for move in moves if move["player"] == player]
    cards = []
    for index, chosen in enumerate(our_moves, start=1):
        ply = int(chosen["moveno"])
        occupied = {}
        for previous in moves:
            if int(previous["moveno"]) >= ply:
                break
            square = (int(previous["row"]), int(previous["col"]))
            occupied[square] = "X" if previous["player"] == player else "O"
        selected = (int(chosen["row"]), int(chosen["col"]))
        prior = moves[ply - 1] if ply else None
        last_op = (int(prior["row"]), int(prior["col"])) if prior else None
        scores = {(int(c["row"]), int(c["col"])): int(c["score"])
                  for c in chosen.get("candidates", [])}
        mini_boards = []
        for outer_row in range(3):
            for outer_col in range(3):
                mini = [occupied.get((outer_row * 3 + inner_row,
                                      outer_col * 3 + inner_col), ".")
                        for inner_row in range(3) for inner_col in range(3)]
                _, status, status_class = mini_status(mini)
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
                        elif square in scores:
                            value = scores[square]
                            classes.append("positive" if value > 0 else
                                           "negative" if value < 0 else "neutral")
                            content = f"{value:+d}" if value else "0"
                            title = f"row {row}, col {col}: score {value}"
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
        chosen_score = scores.get(selected)
        if chosen_score is None:
            score_text = "—"
        else:
            score_text = f"{chosen_score:+d}" if chosen_score else "0"
        cards.append(
            f'<section class="board-card"><h3>Our turn {index} · game ply {ply + 1}</h3>'
            f'<p>{last_text}played <strong>row {selected[0]}, col {selected[1]}</strong> · '
            f'{html.escape(str(chosen["time"]))} ms · {len(scores)} scored choices · '
            f'selected score {score_text}</p>'
            f'<div class="board">{"".join(mini_boards)}</div></section>'
        )
    return '<div class="boards">' + "\n".join(cards) + '</div>'


winner = result["winner"]
winner_text = "draw" if winner is None else f"p{winner}"
identity = header.get(f"p{player} HELLO", "")

# A Forfeit is a technical failure, not a game result, so banner it directly
# under the heading.  The rig records the forfeiter's opponent as the winner.
fail_banner = ""
if result["result type"] == "Forfeit":
    forfeiter = f"p{1 - winner}" if winner in (0, 1) else "an unidentified player"
    fail_banner = (
        '<div class="fail-banner"><strong>TECHNICAL FAILURE &mdash; FORFEIT</strong>'
        f'{forfeiter} forfeited; this is not a completed game result. '
        'See game.log for the rig Forfeit line.</div>')

document = f"""<!doctype html>
<html><head><meta charset="utf-8"><title>Ultimate Tic-Tac-Toe instrument report</title>
<style>
body {{ font: 14px system-ui, sans-serif; margin: 24px; color: #17202a; }}
h1,h2 {{ margin-bottom: .4rem; }} .note {{ color:#566573; }}
.fail-banner {{ margin:12px 0; padding:12px 14px; border:3px solid #b91c1c;
  border-radius:10px; background:#fee2e2; color:#7f1d1d; font-size:15px; line-height:1.4; }}
.fail-banner strong {{ display:block; font-size:17px; letter-spacing:.05em; }}
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
table.summary {{ border-collapse:collapse; margin-top:14px; }}
table.summary th,table.summary td {{ border:1px solid #ccd1d1; padding:5px 10px; text-align:left; }}
table.summary th {{ background:#eaf2f8; }}
</style></head><body>
<h1>Ultimate Tic-Tac-Toe: one-game instrument report</h1>
{fail_banner}
<p class="note">X is the instrumented bot (p{player}); O is the opponent. The purple ring and
LAST label mark the opponent's immediately preceding move; yellow marks our choice.
Strong blue/red/grey borders and X WON/O WON/DRAW badges mark closed small boards.
Numbers are each candidate's root score; green favours us, red favours the opponent.
Hover a cell for its coordinates and score.</p>
{board_cards()}
<h2>Run summary</h2>
<table class="summary">
<tr><th>Run</th><td>{html.escape(header["date/time"])}</td></tr>
<tr><th>Instrumented bot</th><td>p{player} · {html.escape(identity)}</td></tr>
<tr><th>First player</th><td>p{header["first player"]}</td></tr>
<tr><th>Winner</th><td>{winner_text}</td></tr>
<tr><th>Win reason</th><td>{html.escape(result["result type"])}</td></tr>
<tr><th>Total moves</th><td>{result["total moves"]}</td></tr>
<tr><th>Total time</th><td>{result["total time"]} ms</td></tr>
</table>
<p class="note">Generated {html.escape(datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z'))}</p>
</body></html>"""

target.write_text(document)
print(target)
