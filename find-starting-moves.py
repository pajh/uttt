#!/usr/bin/env python3
"""Measure every opening move with matched seeds and bounded parallelism."""
import argparse
import csv
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


def transform(row, col, rotation, reflect):
    for _ in range(rotation):
        row, col = col, 8 - row
    if reflect:
        col = 8 - col
    return row, col


def orbit_id(row, col):
    return min(transform(row, col, rotation, reflect)
               for rotation in range(4) for reflect in (False, True))


def run_opening(args, output, row, col):
    stem = f"r{row}c{col}"
    games_path = output / f"{stem}-games.csv"
    identity_path = output / f"{stem}-identity.tsv"
    log_path = output / f"{stem}.log"
    command = [
        "./bin/gamerig", f"-G{args.games}",
        "--p0", args.p0, "--p1", args.p1,
        "--force-opening", f"{row},{col}",
        "--seed", str(args.seed), "--quiet-bots",
        "--games-csv", str(games_path),
        "--identity-file", str(identity_path),
    ]
    with log_path.open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    records = list(csv.DictReader(games_path.open())) if games_path.exists() else []
    wins = sum(record["winner"] == "0" for record in records)
    losses = sum(record["winner"] == "1" for record in records)
    draws = sum(record["winner"] == "2" for record in records)
    failures = sum(record["reason"] != "played" for record in records)
    grid = (row // 3) * 3 + col // 3
    cell = (row % 3) * 3 + col % 3
    canonical = orbit_id(row, col)
    return {
        "row": row, "col": col, "grid": grid, "cell": cell,
        "orbit": f"r{canonical[0]}c{canonical[1]}",
        "games": len(records), "wins": wins, "losses": losses,
        "draws": draws, "technical_failures": failures,
        "p0_3iar_wins": sum(r["winner"] == "0" and r["win_type"] == "3iar" for r in records),
        "p0_count_wins": sum(r["winner"] == "0" and r["win_type"] == "count" for r in records),
        "match_score": (wins + 0.5 * draws) / len(records) if records else "",
        "exit_status": result.returncode,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-G", "--games", type=int, default=25,
                        help="games per opening (default: 25; 2025 total)")
    parser.add_argument("-j", "--jobs", type=int, default=4,
                        help="concurrent openings (default: 4)")
    parser.add_argument("--seed", type=int, default=20261001,
                        help="base seed reused for every opening")
    parser.add_argument("--p0", default="./bin/ai_search_start --uscale=5 --count-scale=0 --exact-primary=17 --exact-narrow=19")
    parser.add_argument("--p1", default="./bin/orig")
    parser.add_argument("--output", default="starting-moves")
    args = parser.parse_args()
    if args.games < 1 or args.jobs < 1:
        parser.error("games and jobs must be positive")
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "config.json").write_text(json.dumps({
        "games_per_opening": args.games, "jobs": args.jobs, "seed": args.seed,
        "p0": args.p0, "p1": args.p1,
    }, indent=2) + "\n")
    futures = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for row in range(9):
            for col in range(9):
                futures.append(pool.submit(run_opening, args, output, row, col))
        rows = []
        for completed, future in enumerate(as_completed(futures), 1):
            row = future.result()
            rows.append(row)
            print(f"{completed}/81: ({row['row']},{row['col']}) "
                  f"{row['wins']}/{row['losses']}/{row['draws']}", flush=True)
    rows.sort(key=lambda item: (item["row"], item["col"]))
    with (output / "summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    identities = []
    for row in range(9):
        for col in range(9):
            path = output / f"r{row}c{col}-identity.tsv"
            identities.append(path.read_text() if path.exists() else "")
    if not identities[0] or any(identity != identities[0] for identity in identities[1:]):
        raise SystemExit("Bot identity/hello mismatch across openings")
    (output / "metadata.tsv").write_text(identities[0])

    orbit_rows = []
    for orbit in sorted({row["orbit"] for row in rows}):
        members = [row for row in rows if row["orbit"] == orbit]
        games = sum(row["games"] for row in members)
        wins = sum(row["wins"] for row in members)
        losses = sum(row["losses"] for row in members)
        draws = sum(row["draws"] for row in members)
        orbit_rows.append({
            "orbit": orbit,
            "members": " ".join(f"r{row['row']}c{row['col']}" for row in members),
            "openings": len(members), "games": games, "wins": wins,
            "losses": losses, "draws": draws,
            "technical_failures": sum(row["technical_failures"] for row in members),
            "match_score": (wins + 0.5 * draws) / games,
        })
    with (output / "orbits.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(orbit_rows[0]))
        writer.writeheader()
        writer.writerows(orbit_rows)
    if any(row["exit_status"] or row["technical_failures"] or row["games"] != args.games for row in rows):
        raise SystemExit("One or more openings failed; inspect summary.csv and the per-opening logs")
    best = max(rows, key=lambda item: item["match_score"])
    print(f"Best observed opening: row {best['row']} col {best['col']} "
          f"(grid {best['grid']}, cell {best['cell']}), score {best['match_score']:.3f}")
    print(f"Summary: {output / 'summary.csv'}")
    print(f"Symmetry classes: {output / 'orbits.csv'}")


if __name__ == "__main__":
    main()
