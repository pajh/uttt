MM-006-R1MM corrected leaf master certificate: local 100-game gate

Result: 84 wins, 13 losses, 3 draws; 85.5% match score; 0 technical failures.
The four workers ran 25 games each with seeds 20261001, 21261001, 22261001,
and 23261001. The rig alternated starting players within each worker; the
per-game starting_player and trace_hash are preserved in games.csv.

Command: ./bin/gamerig -G25 --p0 './bin/ai_minimax' --p1 './bin/orig' --p1-game-seed --quiet-bots
Bot identity: p0 MM-006-R1MM (from --HELLO and worker logs); orig has no --HELLO.
The source/build identity for this evidence is the reviewed MM-006-R1MM source
at the commit that records this result. The bot reported 340,860,673 scored
positions in 222,520,320 search microseconds: 1,531,818 positions/second.

This is local strength evidence for the corrected turn-ownership fix, not the
requested 1,000-game confirmation. The predecessor MM-005 57/34/9 run remains
documented as invalid because it used the just-applied mover at depth zero.
