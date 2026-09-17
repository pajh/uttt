MM-007-R1MM internal master certificate: local 100-game gate

Result: 74 wins, 21 losses, 5 draws; 76.5% match score; 0 technical
failures.  The four workers ran 25 games each with seeds 20261001,
21261001, 22261001, and 23261001.  Starts alternated within each worker;
the per-game starting_player and trace_hash are preserved in games.csv.

Command: ./bin/gamerig -G25 --p0 './bin/ai_minimax' --p1 './bin/orig' --p1-game-seed --quiet-bots
Bot identity: p0 MM-007-R1MM (from --HELLO and worker identities); orig has no
--HELLO response.  The p0 totals were 451,169,948 scored positions in
218,693,147 search microseconds, or 2,063,027 positions/second.  The
certificate counters were 6,185,717 leaf hits and 2,473,105 internal hits.

Compared with the MM-006 corrected leaf-certificate local gate (84/13/3),
this batch provides no evidence of a strength gain.  The sample is only 100
games and therefore noisy; decision: investigate/revert rather than promote
MM-007 or overclaim a regression from this batch alone.
