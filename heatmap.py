#!/usr/bin/env python3
"""Render heatdump CSV to a standalone HTML replay."""
import csv, html, sys
from collections import defaultdict
from pathlib import Path
rows=list(csv.DictReader(open(sys.argv[1])))
frames=defaultdict(list)
for r in rows:
 if r['player']=='0': frames[int(r['ply'])].append(r)
limit=max([abs(int(r['score'])) for r in rows if r['legal']=='1' and abs(int(r['score']))<60000] or [1]) or 1
out=['''<!doctype html><meta charset="utf-8"><title>Evaluation replay</title>
<style>body{font:15px system-ui;background:#eee;margin:24px}.frames{display:flex;flex-wrap:wrap;gap:24px}.frame{background:white;padding:16px}table{border-collapse:collapse}td{width:48px;height:48px;text-align:center;border:1px solid #bbb;font-weight:500}td:nth-child(3n){border-right:3px solid #333}tr:nth-child(3n) td{border-bottom:3px solid #333}.chosen{outline:4px solid #111;outline-offset:-4px;font-weight:900}.closed{color:#777}small{display:block}</style>
<h1>Evaluation replay</h1><p>Only player 0’s turns are shown; opponent moves are replayed into the next board. Scores are from player 0’s perspective, after equal-depth minimax for every candidate. Hover shows the immediate heuristic components separately. Thick border = actual move. Green = positive, red = negative; colour scale shared across all frames. The actual timed bot may have completed a shallower depth; its chosen move need not match this full-depth replay.</p><div class="frames">''']
for ply, cells in sorted(frames.items()):
 r=cells[0];out.append(f'<section class="frame"><h3>Ply {ply} · player {r["player"]}</h3><small>USCALE {r["uscale"]}; count scale {r["count_scale"]}; search {r.get("plies","1")} plies</small><table>')
 for y in range(9):
  out.append('<tr>')
  for x in range(9):
   r=cells[y*9+x];legal=r['legal']=='1';value=int(r['score'])
   colour='#ddd' if r['closed']=='1' else '#fafafa'
   if legal:
    amount=min(abs(value)/limit,1);shade=int(255-150*amount)
    colour=f'rgb({shade},255,{shade})' if value>=0 else f'rgb(255,{shade},{shade})'
   label=r['score'] if legal else {'0':'','1':'X','2':'O'}[r['mark']]
   tooltip=f"{r['outcome']}: minimax {value}; immediate {r.get('immediate_score',r['score'])}; immediate top {r['top']}; local {r['local']}; count {r['count']}" if legal else 'Unavailable'
   classes=('chosen ' if r['chosen']=='1' else '')+('closed' if r['closed']=='1' else '')
   out.append(f'<td class="{classes}" style="background:{colour}" title="{html.escape(tooltip)}">{label}</td>')
  out.append('</tr>')
 out.append('</table></section>')
out.append('</div>')
path=Path(sys.argv[2] if len(sys.argv)>2 else 'heatmap.html');path.write_text(''.join(out));print(path)
