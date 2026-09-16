#!/usr/bin/env python3
"""Compare the 15 empty-board opening symmetries and normal baseline."""
import argparse
import csv
import json
import subprocess
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

CLASSES = ("BASE", "MM", "MD", "MC", "DM", "DDS", "DDO", "DDA",
           "DCN", "DCF", "CM", "CDN", "CDF", "CCS", "CCO", "CCA")


def run_class(args, output, opening_class):
    games_path = output / f"{opening_class}-games.csv"
    identity_path = output / f"{opening_class}-identity.tsv"
    log_path = output / f"{opening_class}.log"
    opening_args = ["--p0-first"] if opening_class == "BASE" else ["--opening-class", opening_class]
    command = [
        "./bin/gamerig", f"-G{args.games}",
        "--p0", args.p0, "--p1", args.p1,
        *opening_args,
        "--seed", str(args.seed), "--quiet-bots",
        "--games-csv", str(games_path),
        "--identity-file", str(identity_path),
    ]
    with log_path.open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    rows = list(csv.DictReader(games_path.open())) if games_path.exists() else []
    wins = sum(row["winner"] == "0" for row in rows)
    losses = sum(row["winner"] == "1" for row in rows)
    draws = sum(row["winner"] == "2" for row in rows)
    return {
        "class": opening_class, "games": len(rows), "wins": wins,
        "losses": losses, "draws": draws,
        "technical_failures": sum(row["reason"] != "played" for row in rows),
        "p0_3iar_wins": sum(row["winner"] == "0" and row["win_type"] == "3iar" for row in rows),
        "p0_count_wins": sum(row["winner"] == "0" and row["win_type"] == "count" for row in rows),
        "match_score": (wins + 0.5 * draws) / len(rows) if rows else "",
        "unique_openings": len({(row["opening_grid"], row["opening_cell"]) for row in rows}),
        "exit_status": result.returncode,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-G", "--games", type=int, default=200,
                        help="games per policy (default: 200; 3,200 total including baseline)")
    parser.add_argument("-j", "--jobs", type=int, default=4)
    parser.add_argument("--seed", type=int, default=20261001)
    parser.add_argument("--p0", default="./bin/ai_minimax")
    parser.add_argument("--p1", default="./bin/orig")
    parser.add_argument("--output", default="starting-classes")
    args = parser.parse_args()
    if args.games < 1 or args.jobs < 1:
        parser.error("games and jobs must be positive")
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    (output / "config.json").write_text(json.dumps(vars(args), indent=2) + "\n")
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(run_class, args, output, name): name for name in CLASSES}
        summaries = []
        for future in as_completed(futures):
            result = future.result()
            summaries.append(result)
            print(f"{result['class']}: {result['wins']}/{result['losses']}/{result['draws']} "
                  f"from {result['unique_openings']} sampled openings", flush=True)
    summaries.sort(key=lambda row: CLASSES.index(row["class"]))
    with (output / "summary.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summaries[0]))
        writer.writeheader(); writer.writerows(summaries)
    samples = []
    for name in CLASSES:
        games = list(csv.DictReader((output / f"{name}-games.csv").open()))
        keys = sorted({(row["opening_grid"], row["opening_cell"]) for row in games})
        for grid, cell in keys:
            selected = [row for row in games if row["opening_grid"] == grid and row["opening_cell"] == cell]
            wins = sum(row["winner"] == "0" for row in selected)
            draws = sum(row["winner"] == "2" for row in selected)
            samples.append({
                "class": name, "grid": grid, "cell": cell,
                "row": selected[0]["opening_row"], "col": selected[0]["opening_col"],
                "samples": len(selected), "wins": wins,
                "losses": sum(row["winner"] == "1" for row in selected),
                "draws": draws, "match_score": (wins + 0.5 * draws) / len(selected),
            })
    with (output / "openings.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(samples[0]))
        writer.writeheader(); writer.writerows(samples)
    identities = [(output / f"{name}-identity.tsv").read_text() for name in CLASSES]
    if not identities[0] or any(identity != identities[0] for identity in identities[1:]):
        raise SystemExit("Bot identity/hello mismatch across opening classes")
    (output / "metadata.tsv").write_text(identities[0])
    if any(row["exit_status"] or row["games"] != args.games for row in summaries):
        raise SystemExit("One or more classes was incomplete; inspect summary.csv and logs")
    best = max(summaries, key=lambda row: row["match_score"])
    print(f"Best observed class: {best['class']} score={best['match_score']:.3f}")
    print(f"Summary: {output / 'summary.csv'}")
    print(f"Sample frequencies: {output / 'openings.csv'}")


if __name__ == "__main__":
    main()
