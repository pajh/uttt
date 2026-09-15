#!/usr/bin/env python3
"""Combine four rig game tables; player 0 is the challenger."""
import csv
import sys
from pathlib import Path

folder = Path(sys.argv[1])
combined = []
summary = []
for worker in range(1, 5):
    path = folder / f'worker-{worker}-games.csv'
    rows = list(csv.DictReader(path.open())) if path.exists() else []
    for row in rows:
        combined.append({'worker': worker, **row})
    wins = sum(r['winner'] == '0' for r in rows)
    losses = sum(r['winner'] == '1' for r in rows)
    draws = sum(r['winner'] == '2' for r in rows)
    failures = sum(r['reason'] != 'played' for r in rows)
    summary.append(dict(worker=worker, games=len(rows), wins=wins, losses=losses,
                        draws=draws, technical_failures=failures))
for row in summary:
    rows = [r for r in combined if r['worker']==row['worker']]
    for player in ('0','1'):
        for kind in ('3iar','count'):
            row[f'p{player}_{kind}_wins'] = sum(r['winner']==player and r.get('win_type')==kind for r in rows)
        row[f'p{player}_dfs_failed'] = sum(int(r.get(f'p{player}_dfs_failed',0)) for r in rows)
        row[f'p{player}_dfs_failed_primary'] = sum(int(r.get(f'p{player}_dfs_failed_primary',0)) for r in rows)
        row[f'p{player}_dfs_failed_narrow'] = sum(int(r.get(f'p{player}_dfs_failed_narrow',0)) for r in rows)
identities = []
for worker in range(1, 5):
    path = folder / f'worker-{worker}-identity.tsv'
    records = {}
    if path.exists():
        for line in path.read_text().splitlines():
            player, bot_id, hello = line.split('\t', 2)
            records[player] = (bot_id, hello)
    if set(records) != {'0', '1'}:
        sys.exit(f'Missing verified identities for worker {worker}')
    identities.append(records)
if any(record != identities[0] for record in identities):
    sys.exit('BOT IDENTITY/HELLO MISMATCH across workers')
for row in summary:
    usage_path = folder / f"worker-{row['worker']}-identity.tsv.usage.csv"
    usage = list(csv.DictReader(usage_path.open())) if usage_path.exists() else []
    row['p0_evaluations'] = sum(int(r['evaluations']) for r in usage if r['player']=='0')
    row['p0_count_differential_evaluations'] = sum(int(r['count_differential_evaluations']) for r in usage if r['player']=='0')
total = {'worker': 'TOTAL'}
for field in ('p0_evaluations','p0_count_differential_evaluations'):
    total[field] = sum(r[field] for r in summary)

for field in summary[0]:
    if field!='worker': total[field]=sum(r[field] for r in summary)
summary.append(total)
for row in summary:
    row['match_score'] = ((row['wins'] + .5 * row['draws']) / row['games']) if row['games'] else ''
with (folder / 'summary.csv').open('w', newline='') as out:
    metadata = csv.writer(out)
    metadata.writerow(['metadata','bot_id','hello'])
    for player in ('0','1'):
        metadata.writerow([f'p{player}', *identities[0][player]])
    metadata.writerow([])
    writer = csv.DictWriter(out, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
if combined:
    with (folder / 'games.csv').open('w', newline='') as out:
        writer = csv.DictWriter(out, fieldnames=list(combined[0]))
        writer.writeheader()
        writer.writerows(combined)
print(f"TOTAL: {total['wins']} wins / {total['losses']} losses / {total['draws']} draws; "
      f"{total['games']} games; {total['technical_failures']} technical failures")
print(f"Summary: {folder / 'summary.csv'}")
if any(not r['games'] for r in summary[:-1]):
    sys.exit('Missing game results from one or more workers; inspect status.csv and logs.')
