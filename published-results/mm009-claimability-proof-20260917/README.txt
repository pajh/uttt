MM-009-R1MM local claimability-proof gate (2026-09-17)

This directory preserves the reproducible four-worker, 100-game comparison
against the preserved orig bot.  Each worker played 25 games with seeds
20261001, 21261001, 22261001, and 23261001, respectively; starts alternated
per game (52 MM-009-first, 48 orig-first overall).  The rig passed the same
seed to orig via --p1-game-seed.  Command: see command.txt.

Result: MM-009-R1MM 77 wins / 16 losses / 7 draws (80.5% match score), with
zero technical failures, illegal moves, timeouts, or nonzero worker exits.
All four workers identified p0 as MM-009-R1MM; orig emitted no --HELLO
response.  The aggregate game and summary data are in games.csv and
summary.csv; per-worker identities are in worker-*-identity.tsv.

MM-009 totals: 433,501,197 scored positions, 220,316,857 search_us,
1,967,626 scored positions/s, 37,252,214 cache hits, 5,695,619 leaf
certificate hits, and 2,548,247 internal certificate hits.  The measured
scored-position throughput is 3.75% below MM-008, with all four workers
lower.  This is not a total-node speed comparison because proof returns are
not counted as scored positions.  The 100-game strength improvement over
MM-008 is inconclusive; investigate before promotion.

latest-analysis.html is the readable rendered report.  run-source.txt,
run-timing.csv, seeds.csv, status.csv, and the identity files retain run
provenance; starting-player values are retained in games.csv.
