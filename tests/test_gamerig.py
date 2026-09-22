#!/usr/bin/env python3
"""End-to-end tests for the single-game gamerig JSON runner.

Run from anywhere:

    tests/test_gamerig.py

The script builds the referee and the bots it needs, then drives real bot
processes through gamerig and checks the JSON summary and exit status.  It uses
only the Python standard library.
"""
import json
import subprocess
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAMERIG = ROOT / "bin" / "gamerig"

ORIG = "./bin/orig"
RANDOM = "./bin/ai_random"
FAILBOT = "./bin/ai_failbot"
NEGAMAX = "./bin/ai_negamax"
SEED = "--seed 1"

SCHEMA_HEADER_KEYS = {
    "p0 command line", "p1 command line", "p0 HELLO", "p1 HELLO",
    "date/time", "first player",
}
SCHEMA_RESULT_KEYS = {"winner", "total moves", "result type", "total time"}
SCHEMA_MOVE_KEYS = {"moveno", "player", "row", "col", "time"}
RESULT_TYPES = {"3inARow", "CountVictory", "draw", "Forfeit"}


def run_rig(*args):
    return subprocess.run(
        [str(GAMERIG), *args],
        cwd=ROOT,
        capture_output=True,
        text=True,
        timeout=180,
    )


def hung_failbot_pids():
    """PIDs of leftover `ai_failbot --hung` processes, found via /proc."""
    pids = set()
    for entry in Path("/proc").iterdir():
        if not entry.name.isdigit():
            continue
        try:
            cmdline = (entry / "cmdline").read_bytes().replace(b"\x00", b" ").decode(errors="replace")
        except OSError:
            continue
        if "ai_failbot" in cmdline and "--hung" in cmdline:
            pids.add(int(entry.name))
    return pids


class GamerigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.run(
            ["make", "bin/gamerig", "bin/orig", "bin/ai_random", "bin/ai_failbot"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )
        # A small evaluation budget keeps the instrumented games quick while
        # still producing root candidates.
        subprocess.run(
            ["make", "-B", "L=1", "E=1", "MAX_SCORE=20000", "bin/ai_negamax"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

    @classmethod
    def tearDownClass(cls):
        # Restore the plain (non-instrumented) negamax build.
        subprocess.run(
            ["make", "-B", "bin/ai_negamax"],
            cwd=ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

    def assert_game_schema(self, proc):
        self.assertEqual(proc.returncode, 0, proc.stderr)
        game = json.loads(proc.stdout)

        self.assertEqual(set(game), {"header", "moves", "result"})

        header = game["header"]
        self.assertIn(set(header), (SCHEMA_HEADER_KEYS, SCHEMA_HEADER_KEYS | {"instrumented"}))
        self.assertIn(header["first player"], (0, 1))
        if "instrumented" in header:
            self.assertIn(header["instrumented"], (0, 1))
        for key in ("p0 command line", "p1 command line", "p0 HELLO", "p1 HELLO", "date/time"):
            self.assertIsInstance(header[key], str)
            self.assertTrue(header[key])

        moves = game["moves"]
        expected_player = header["first player"]
        for index, move in enumerate(moves):
            keys = set(move)
            self.assertTrue(keys == SCHEMA_MOVE_KEYS or keys == SCHEMA_MOVE_KEYS | {"candidates"},
                            keys)
            self.assertEqual(move["moveno"], index)
            self.assertEqual(move["player"], expected_player)
            self.assertIn(move["row"], range(9))
            self.assertIn(move["col"], range(9))
            self.assertIsInstance(move["time"], int)
            self.assertGreaterEqual(move["time"], 0)
            if "candidates" in move:
                candidates = move["candidates"]
                self.assertIsInstance(candidates, list)
                self.assertTrue(candidates)
                for candidate in candidates:
                    self.assertEqual(set(candidate), {"row", "col", "score"})
                    self.assertIn(candidate["row"], range(9))
                    self.assertIn(candidate["col"], range(9))
                    self.assertIsInstance(candidate["score"], int)
            expected_player = 1 - expected_player

        result = game["result"]
        self.assertEqual(set(result), SCHEMA_RESULT_KEYS)
        self.assertIn(result["winner"], (0, 1, None))
        self.assertIn(result["result type"], RESULT_TYPES)
        self.assertIsInstance(result["total moves"], int)
        self.assertEqual(result["total moves"], len(moves))
        self.assertIsInstance(result["total time"], int)
        self.assertGreaterEqual(result["total time"], 0)
        if result["result type"] == "draw":
            self.assertIsNone(result["winner"])
        else:
            self.assertIn(result["winner"], (0, 1))
        return game

    def test_valid_game(self):
        proc = run_rig(ORIG, SEED, RANDOM, SEED, "0")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["header"]["p0 HELLO"], "ORIG001")
        self.assertEqual(game["header"]["p1 HELLO"], "RANDOM001")
        self.assertEqual(game["header"]["p0 command line"], "./bin/orig --seed 1")
        self.assertEqual(game["header"]["p1 command line"], "./bin/ai_random --seed 1")
        self.assertEqual(game["header"]["first player"], 0)
        self.assertNotIn("instrumented", game["header"])
        self.assertIn(game["result"]["result type"], ("3inARow", "CountVictory", "draw"))
        self.assertGreater(game["result"]["total moves"], 0)

    def test_bad_move_forfeits(self):
        proc = run_rig(FAILBOT, "--bad_move", ORIG, SEED, "0")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        self.assertEqual(game["result"]["total moves"], 0)

    def test_crash_forfeits(self):
        proc = run_rig(FAILBOT, "--crash", ORIG, SEED, "0")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        self.assertEqual(game["result"]["total moves"], 0)

    def test_late_bad_move_when_starting_forfeits_after_two_moves(self):
        proc = run_rig(FAILBOT, "--late_bad_move", ORIG, SEED, "0")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        self.assertEqual(game["result"]["total moves"], 2)

    def test_late_bad_move_when_second_forfeits_after_one_move(self):
        proc = run_rig(ORIG, SEED, FAILBOT, "--late_bad_move", "0")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 0)
        self.assertEqual(game["result"]["total moves"], 1)

    def test_first_player_can_be_one(self):
        proc = run_rig(FAILBOT, "--late_bad_move", ORIG, SEED, "1")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["header"]["first player"], 1)
        self.assertEqual(game["moves"][0]["player"], 1)

    def test_missing_hello_aborts_without_json(self):
        proc = run_rig("/bin/true", "", ORIG, SEED, "0")
        self.assertEqual(proc.returncode, 2, proc.stderr)
        self.assertEqual(proc.stdout, "")
        self.assertIn("player 0 did not provide a valid --HELLO reply", proc.stderr)

    def test_missing_binary_aborts_as_missing_hello(self):
        proc = run_rig("./bin/does_not_exist", "", ORIG, SEED, "0")
        self.assertEqual(proc.returncode, 2, proc.stderr)
        self.assertEqual(proc.stdout, "")
        self.assertIn("player 0 did not provide a valid --HELLO reply", proc.stderr)

    def test_hung_bot_is_killed_and_reaped(self):
        before = hung_failbot_pids()
        start = time.monotonic()
        proc = run_rig(FAILBOT, "--hung", ORIG, SEED, "0", "--relaxed", "200")
        elapsed = time.monotonic() - start
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        # The rig must not wait out the 20 s hang: it forfeits, kills the bot,
        # and reaps it before returning.
        self.assertLess(elapsed, 5.0)
        self.assertEqual(hung_failbot_pids() - before, set(),
                         "hung failbot process was not cleaned up")

    def test_slow_bot_times_increase_and_are_flagged(self):
        proc = run_rig(FAILBOT, "--goslow", RANDOM, SEED, "0", "--relaxed", "200")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        fail_times = [m["time"] for m in game["moves"] if m["player"] == 0]
        self.assertGreaterEqual(len(fail_times), 8)
        self.assertTrue(all(b > a for a, b in zip(fail_times, fail_times[1:])),
                        f"slow bot move times not increasing: {fail_times}")
        self.assertGreaterEqual(proc.stderr.count("WARNING"), 5)
        self.assertIn("Forfeit: player 0", proc.stderr)

    def test_goslow_forfeits_when_over_cap(self):
        proc = run_rig(FAILBOT, "--goslow", ORIG, SEED, "0", "--relaxed", "10")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        self.assertEqual(game["result"]["total moves"], 0)

    def test_hung_forfeits_at_cap(self):
        proc = run_rig(FAILBOT, "--hung", ORIG, SEED, "0", "--relaxed", "200")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")
        self.assertEqual(game["result"]["winner"], 1)
        self.assertEqual(game["result"]["total moves"], 0)

    def test_relaxed_option_accepted(self):
        proc = run_rig(FAILBOT, "--bad_move", ORIG, SEED, "0", "--relaxed", "5000")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["result"]["result type"], "Forfeit")

    def test_instrument_player0_candidates(self):
        proc = run_rig(NEGAMAX, SEED, ORIG, SEED, "0", "--instrument", "0")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["header"]["instrumented"], 0)
        instrumented = [m for m in game["moves"] if m["player"] == 0 and "candidates" in m]
        self.assertTrue(instrumented)
        for move in instrumented:
            squares = [(c["row"], c["col"]) for c in move["candidates"]]
            self.assertIn((move["row"], move["col"]), squares)
        for move in game["moves"]:
            if move["player"] == 1:
                self.assertNotIn("candidates", move)

    def test_instrument_player1_candidates(self):
        proc = run_rig(ORIG, SEED, NEGAMAX, SEED, "1", "--instrument", "1")
        game = self.assert_game_schema(proc)
        self.assertEqual(game["header"]["instrumented"], 1)
        instrumented = [m for m in game["moves"] if m["player"] == 1 and "candidates" in m]
        self.assertTrue(instrumented)
        for move in instrumented:
            squares = [(c["row"], c["col"]) for c in move["candidates"]]
            self.assertIn((move["row"], move["col"]), squares)

    def test_instrument_requires_capability(self):
        proc = run_rig(ORIG, SEED, RANDOM, SEED, "0", "--instrument", "0")
        self.assertEqual(proc.returncode, 2, proc.stderr)
        self.assertEqual(proc.stdout, "")
        self.assertIn("does not advertise instrumentation", proc.stderr)

    def test_usage_errors(self):
        self.assertEqual(run_rig().returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "2").returncode, 2)
        self.assertEqual(run_rig("", "", ORIG, SEED, "0").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED).returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--relaxed", "abc").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--relaxed", "0").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--relaxed").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--bogus", "1").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--instrument").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--instrument", "2").returncode, 2)
        self.assertEqual(run_rig(ORIG, SEED, RANDOM, SEED, "0", "--instrument", "x").returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
