#!/usr/bin/env python3
"""Combine rig game tables; player 0 is the challenger."""
import csv
import sys
from pathlib import Path

folder = Path(sys.argv[1])
seeds_path = folder / 'seeds.csv'
if not seeds_path.exists():
    sys.exit(f'Missing worker list: {seeds_path}')
seed_rows = list(csv.reader(seeds_path.open()))
if seed_rows and seed_rows[0] == ['worker', 'seed']:
    seed_rows.pop(0)
worker_numbers = [int(row[0]) for row in seed_rows]
if not worker_numbers or worker_numbers != list(range(1, len(worker_numbers) + 1)):
    sys.exit(f'Invalid worker list in {seeds_path}')
combined = []
summary = []
for worker in worker_numbers:
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
        reported = [r for r in rows if r.get(f'p{player}_stats_present') == '1']
        row[f'p{player}_stats_games'] = len(reported)
        for field in ('search_us', 'positions_scored', 'cache_hits'):
            row[f'p{player}_{field}'] = sum(int(r[f'p{player}_{field}']) for r in reported)
        search_us = row[f'p{player}_search_us']
        row[f'p{player}_positions_per_second'] = (
            round(row[f'p{player}_positions_scored'] * 1000000 / search_us)
            if search_us else '')
identities = []
for worker in worker_numbers:
    path = folder / f'worker-{worker}-identity.tsv'
    records = {}
    if path.exists():
        for line in path.read_text().splitlines():
            fields = line.split('\t', 2)
            if len(fields) == 2:
                player, hello = fields
                records[player] = (hello, hello)
            elif len(fields) == 3:
                player, bot_id, hello = fields
                records[player] = (bot_id, hello)
            else:
                sys.exit(f'Invalid identity record in {path}: {line!r}')
    if set(records) != {'0', '1'}:
        sys.exit(f'Missing verified identities for worker {worker}')
    identities.append(records)
if any(record != identities[0] for record in identities):
    sys.exit('BOT IDENTITY/HELLO MISMATCH across workers')
total = {'worker': 'TOTAL'}
for field in summary[0]:
    if field != 'worker' and not field.endswith('_positions_per_second'):
        total[field] = sum(r[field] for r in summary)
for player in ('0', '1'):
    search_us = total[f'p{player}_search_us']
    total[f'p{player}_positions_per_second'] = (
        round(total[f'p{player}_positions_scored'] * 1000000 / search_us)
        if search_us else '')
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
win_3 = sum(r['winner'] == '0' and r.get('win_type') == '3iar' for r in combined)
win_c = sum(r['winner'] == '0' and r.get('win_type') == 'count' for r in combined)
win_f = sum(r['winner'] == '0' and r.get('reason') != 'played' for r in combined)
loss_3 = sum(r['winner'] == '1' and r.get('win_type') == '3iar' for r in combined)
loss_c = sum(r['winner'] == '1' and r.get('win_type') == 'count' for r in combined)
loss_f = sum(r['winner'] == '1' and r.get('reason') != 'played' for r in combined)
print(f"Played: {total['games']}  Wins: {total['wins']} "
      f"(3:{win_3}, C:{win_c}, F:{win_f})  "
      f"Losses: {total['losses']} (3:{loss_3}, C:{loss_c}, F:{loss_f})  "
      f"Draws: {total['draws']}")
print(f"Summary: {folder / 'summary.csv'}")
if any(not r['games'] for r in summary[:-1]):
    sys.exit('Missing game results from one or more workers; inspect status.csv and logs.')
