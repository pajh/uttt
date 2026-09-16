#!/usr/bin/env python3
"""Render a multi-rig summary CSV as a self-contained HTML report."""
import csv
import html
import sys
from datetime import datetime
from pathlib import Path


def safe(value):
    return html.escape(str(value))


def number(value):
    return f'{int(value):,}' if value not in ('', None) else 'not recorded'


def local_time(epoch):
    return datetime.fromtimestamp(int(epoch)).astimezone().strftime('%Y-%m-%d %H:%M:%S %Z %z')


source, target = map(Path, sys.argv[1:3])
source_details = {}
source_path = source.parent / 'run-source.txt'
if source_path.exists():
    source_details = dict(line.split('=', 1) for line in source_path.read_text().splitlines() if '=' in line)
if source_details.get('origin') == 'github':
    run_url = source_details.get('url', '')
    origin_label = (f'GitHub Actions run {safe(source_details.get("run_id", "unknown"))}'
                    + (f' · <a href="{safe(run_url)}">Open on GitHub</a>' if run_url.startswith('https://') else ''))
else:
    origin_label = 'Local run'
timing_path = source.parent / 'run-timing.csv'
started = finished = duration = 'not recorded'
if timing_path.exists():
    with timing_path.open(newline='') as timing_file:
        timing = next(csv.DictReader(timing_file), {})
    if timing.get('started_epoch'):
        started = local_time(timing['started_epoch'])
    if timing.get('finished_epoch'):
        finished = local_time(timing['finished_epoch'])
    if timing.get('started_epoch') and timing.get('finished_epoch'):
        seconds = max(0, int(timing['finished_epoch']) - int(timing['started_epoch']))
        duration = f'{seconds // 60:04d}:{seconds % 60:02d}'
generated = datetime.now().astimezone().strftime('%Y-%m-%d %H:%M:%S %Z %z')
with source.open(newline='') as stream:
    records = list(csv.reader(stream))
metadata = {row[0]: row[2] for row in records[1:3] if len(row) >= 3}
table_start = next(i for i, row in enumerate(records) if row and row[0] == 'worker')
header = records[table_start]
rows = [dict(zip(header, row)) for row in records[table_start + 1:] if row]
total = next(row for row in rows if row['worker'] == 'TOTAL')
games = int(total['games'])
parts = [
    ('3 in a row wins', int(total['p0_3iar_wins']), '#168a56'),
    ('Count wins', int(total['p0_count_wins']), '#67b880'),
    ('Other wins', int(total['wins']) - int(total['p0_3iar_wins']) - int(total['p0_count_wins']), '#a1d4ab'),
    ('Draws', int(total['draws']), '#aaa'),
    ('3 in a row losses', int(total['p1_3iar_wins']), '#c04c4c'),
    ('Count losses', int(total['p1_count_wins']), '#e48470'),
    ('Other losses', int(total['losses']) - int(total['p1_3iar_wins']) - int(total['p1_count_wins']), '#ecb4aa'),
]
bar = ''.join(f'<span title="{safe(label)}: {count}" style="width:{count / games * 100 if games else 0:.3f}%;background:{color}"></span>'
              for label, count, color in parts)
legend = ''.join(f'<li><b style="background:{color}"></b>{safe(label)}: {count:,}</li>'
                 for label, count, color in parts)
performance = ''
for player in ('0', '1'):
    reported = int(total.get(f'p{player}_stats_games', 0) or 0)
    performance += (f'<tr><td>p{player} — {safe(metadata.get("p" + player, "unknown"))}</td>'
                    f'<td>{reported:,} / {games:,}</td>'
                    f'<td>{number(total.get(f"p{player}_search_us")) if reported else "not recorded"}</td>'
                    f'<td>{number(total.get(f"p{player}_positions_scored")) if reported else "not recorded"}</td>'
                    f'<td>{number(total.get(f"p{player}_cache_hits")) if reported else "not recorded"}</td>'
                    f'<td>{number(total.get(f"p{player}_positions_per_second")) if reported else "not recorded"}</td></tr>')
workers = ''.join(
    f'<tr><td>{safe(row["worker"])}</td><td>{number(row["games"])}</td>'
    f'<td>{number(row["wins"])}</td><td>{number(row["losses"])}</td>'
    f'<td>{number(row["draws"])}</td><td>{float(row["match_score"])*100:.1f}%</td></tr>'
    for row in rows)
document = f'''<!doctype html>
<html lang="en"><meta charset="utf-8"><title>Multi-rig report</title>
<style>body{{font:16px system-ui,sans-serif;max-width:1050px;margin:3rem auto;padding:0 1rem;color:#202626}}
h1,h2{{margin-bottom:.4rem}}.muted{{color:#667}}.bar{{display:flex;height:34px;border-radius:6px;overflow:hidden;background:#ddd}}
.bar span{{display:block}}ul{{display:flex;flex-wrap:wrap;gap:.5rem 1.5rem;padding:0;list-style:none}}li b{{display:inline-block;width:.8em;height:.8em;margin-right:.4em}}
table{{border-collapse:collapse;width:100%;margin:1rem 0 2rem}}td,th{{border-bottom:1px solid #ddd;text-align:right;padding:.55rem}}td:first-child,th:first-child{{text-align:left}}
.freshness{{background:#f2f6f5;border-left:4px solid #168a56;padding:.8rem 1rem;margin:1rem 0 1.5rem}}
.freshness p{{margin:.2rem 0}}
</style><h1>Multi-rig report</h1>
<p><strong>Source:</strong> {origin_label}</p>
<div class="freshness"><p><strong>Run started:</strong> {safe(started)}</p>
<p><strong>Run finished:</strong> {safe(finished)}</p>
<p><strong>Duration (MMMM:SS):</strong> {safe(duration)}</p>
<p><strong>HTML generated:</strong> {safe(generated)}</p></div>
<p class="muted">p0: {safe(metadata.get('p0','unknown'))} · p1: {safe(metadata.get('p1','unknown'))}</p>
<h2>{games:,} games · {int(total['wins']):,} wins · {int(total['losses']):,} losses · {int(total['draws']):,} draws</h2>
<p>Match score: {float(total['match_score'])*100:.1f}% (draws count as half a win)</p><div class="bar" role="img" aria-label="Results split by outcome">{bar}</div><ul>{legend}</ul>
<h2>Search performance</h2><p class="muted">Timed search only; positions scored are leaf scoring calls. Rate is total positions ÷ total timed-search seconds. Missing bot records are not treated as zero.</p>
<table><tr><th>Bot</th><th>Games reported</th><th>Search µs</th><th>Positions scored</th><th>Cache hits</th><th>Positions/s</th></tr>{performance}</table>
<h2>By worker</h2><table><tr><th>Worker</th><th>Games</th><th>Wins</th><th>Losses</th><th>Draws</th><th>Match score</th></tr>{workers}</table>
<p class="muted">Source: {safe(source)}</p></html>'''
if sys.argv[2] == '--check':
    print('Report rendering check passed')
else:
    target.write_text(document)
    print(f'Report: {target}')
