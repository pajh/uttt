#!/usr/bin/env node
// Checks the diff-monotonicity claim for the subboard scorer in
// subboard-score.html, exhaustively over data/unresolved-subboards.json.
//
// Claim under test: for an open (non-closed) child board, placing an X never
// raises the parent diff (xChild - oChild <= xParent - oParent) and placing an
// O never lowers it (oChild - xChild >= oParent - xParent). Closed children
// (either player has a completed line, or the grid is full) are counted
// separately and are not subject to the inequality. A null diff on the parent
// or on an open child makes the comparison undefined; those are counted too.
//
// The scorer is NOT reimplemented here: the pure prefix of the page's inline
// script is extracted and evaluated in a Node vm, which ends at the first DOM
// access, and scoreBoard/classifyBoard are taken from that context.
//
// Exit status is nonzero only for execution or data errors. Found
// counterexamples are reported and still exit 0.
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const ROOT = path.resolve(__dirname, "..");
const HTML_PATH = path.join(ROOT, "subboard-score.html");
const DATA_PATH = path.join(ROOT, "data", "unresolved-subboards.json");

const DOM_ANCHOR = "const boardElement = document.getElementById";
const CLOSED_KINDS = new Set(["both-win", "self-win", "opponent-win", "draw"]);
const EXAMPLES = 5;

// --- scorer, extracted verbatim from the page -------------------------------

function loadScorer() {
  const html = fs.readFileSync(HTML_PATH, "utf8");
  const open = html.search(/<script[^>]*>/);
  if (open < 0) {
    throw new Error(`${HTML_PATH}: no inline <script> block`);
  }
  const bodyStart = html.indexOf(">", open) + 1;
  const bodyEnd = html.indexOf("</script>", bodyStart);
  if (bodyEnd < 0) {
    throw new Error(`${HTML_PATH}: unterminated <script> block`);
  }

  const body = html.slice(bodyStart, bodyEnd);
  const cut = body.indexOf(DOM_ANCHOR);
  if (cut < 0) {
    throw new Error(`${HTML_PATH}: DOM anchor not found, refusing to guess the pure region`);
  }
  const pure = body.slice(0, cut);
  if (/\bdocument\b|\bwindow\b/.test(pure)) {
    throw new Error("pure region still references the DOM");
  }

  const context = vm.createContext({});
  vm.runInContext(
    pure +
      "\nthis.scoreBoard = scoreBoard;" +
      "\nthis.classifyBoard = classifyBoard;" +
      "\nthis.EMPTY = EMPTY; this.X_PLAYER = X_PLAYER; this.O_PLAYER = O_PLAYER;",
    context,
    { filename: "subboard-score.html#pure-scorer" }
  );
  return context;
}

const scorer = loadScorer();
const { scoreBoard, classifyBoard, EMPTY, X_PLAYER, O_PLAYER } = scorer;

const MARKS = { ".": EMPTY, X: X_PLAYER, O: O_PLAYER };
const CHARS = { [X_PLAYER]: "X", [O_PLAYER]: "O" };

function cellsOf(board) {
  return Array.from(board, (character) => {
    const mark = MARKS[character];
    if (mark === undefined) {
      throw new Error(`illegal character ${JSON.stringify(character)} in board ${board}`);
    }
    return mark;
  });
}

// scoreBoard and classifyBoard are pure, so one memo per (board, player) is
// exact and keeps the 100k-plus of calls affordable. key = board + player.
const scoreMemo = new Map();
const kindMemo = new Map();

function scoreOf(board, player) {
  const key = board + CHARS[player];
  let score = scoreMemo.get(key);
  if (score === undefined) {
    score = scoreBoard(cellsOf(board), player);
    scoreMemo.set(key, score);
  }
  return score;
}

function kindOf(board) {
  let kind = kindMemo.get(board);
  if (kind === undefined) {
    kind = classifyBoard(cellsOf(board), X_PLAYER).kind;
    kindMemo.set(board, kind);
  }
  return kind;
}

function unitsOf(board, player, who, out) {
  const units = scoreOf(board, player).scoreUnits;
  if (units !== null && !Number.isInteger(units)) {
    throw new Error(`non-integer ${who} scoreUnits ${units} on ${board}`);
  }
  return units;
}

// --- catalog ----------------------------------------------------------------

const catalog = JSON.parse(fs.readFileSync(DATA_PATH, "utf8"));
if (!catalog || !Array.isArray(catalog.subboards)) {
  throw new Error(`${DATA_PATH}: no subboards array`);
}
const records = catalog.subboards;
const kept = catalog.metadata && catalog.metadata.kept;
if (Number.isInteger(kept) && kept !== records.length) {
  throw new Error(`data error: metadata.kept ${kept} != ${records.length} records`);
}
for (const record of records) {
  if (typeof record.board !== "string" || record.board.length !== 9) {
    throw new Error(`data error: bad board ${JSON.stringify(record && record.board)}`);
  }
}

// --- check ------------------------------------------------------------------

const parentKinds = new Map();
const childKinds = new Map();
let children = 0;
let closed = 0;
let comparisonsX = 0;
let comparisonsO = 0;
let violationsX = 0;
let violationsO = 0;
let undefParentOnly = 0;
let undefChildOnly = 0;
let undefBoth = 0;
let undefX = 0;
let undefO = 0;

const exViolationX = [];
const exViolationO = [];
const exUndef = [];

// Records are visited in board order (then cell index), so the first examples
// kept are the lexicographically first ones.
const ordered = records
  .map((record, index) => ({ record, index }))
  .sort((a, b) =>
    a.record.board < b.record.board ? -1 : a.record.board > b.record.board ? 1 : a.index - b.index
  );

for (const { record } of ordered) {
  const board = record.board;
  const parentX = unitsOf(board, X_PLAYER, "X");
  const parentO = unitsOf(board, O_PLAYER, "O");
  const parentDiff = parentX !== null && parentO !== null ? parentX - parentO : null;

  const parentKind = kindOf(board);
  parentKinds.set(parentKind, (parentKinds.get(parentKind) || 0) + 1);

  for (let cell = 0; cell < 9; cell++) {
    if (board[cell] !== ".") {
      continue;
    }
    for (const mark of [X_PLAYER, O_PLAYER]) {
      const child = board.slice(0, cell) + CHARS[mark] + board.slice(cell + 1);
      children += 1;

      const childX = unitsOf(child, X_PLAYER, "X");
      const childO = unitsOf(child, O_PLAYER, "O");
      const childDiff = childX !== null && childO !== null ? childX - childO : null;

      const childKind = kindOf(child);
      childKinds.set(childKind, (childKinds.get(childKind) || 0) + 1);
      if (CLOSED_KINDS.has(childKind)) {
        closed += 1;
        continue;
      }

      const isX = mark === X_PLAYER;
      const report = {
        board,
        cell,
        mark: isX ? "X" : "O",
        u0: record.U0 === true,
        u1: record.U1 === true,
        childKind,
        parentX,
        parentO,
        parentDiff,
        childX,
        childO,
        childDiff
      };

      if (parentDiff === null || childDiff === null) {
        if (parentDiff === null && childDiff === null) {
          undefBoth += 1;
          report.cause = "both-null";
        } else if (parentDiff === null) {
          undefParentOnly += 1;
          report.cause = "parent-null";
        } else {
          undefChildOnly += 1;
          report.cause = "child-null";
        }
        if (isX) {
          undefX += 1;
        } else {
          undefO += 1;
        }
        if (exUndef.length < EXAMPLES) {
          exUndef.push(report);
        }
        continue;
      }

      if (isX) {
        comparisonsX += 1;
        if (childDiff > parentDiff) {
          violationsX += 1;
          if (exViolationX.length < EXAMPLES) {
            exViolationX.push(report);
          }
        }
      } else {
        comparisonsO += 1;
        if (childDiff < parentDiff) {
          violationsO += 1;
          if (exViolationO.length < EXAMPLES) {
            exViolationO.push(report);
          }
        }
      }
    }
  }
}

// --- report -----------------------------------------------------------------

function counts(map) {
  return [...map.entries()].sort().map(([kind, n]) => `${kind}=${n}`).join(" ");
}

function example(e) {
  const diff = (a, b) => `${a === null ? "null" : a} -> ${b === null ? "null" : b}`;
  return (
    `${e.board} cell=${e.cell} mark=${e.mark} kind=${e.childKind} U0=${e.u0 ? 1 : 0} U1=${e.u1 ? 1 : 0}` +
    ` | X ${diff(e.parentX, e.childX)} O ${diff(e.parentO, e.childO)} diff ${diff(e.parentDiff, e.childDiff)}`
  );
}

console.log(`catalog records: ${records.length} (metadata.kept=${kept})`);
console.log(`parent kinds: ${counts(parentKinds)}`);
console.log(`child boards simulated: ${children} (${counts(childKinds)})`);
console.log(`closed children (line or full, excluded from the inequality): ${closed}`);
console.log(
  `undefined comparisons: ${undefParentOnly + undefChildOnly + undefBoth}` +
    ` (parent-null=${undefParentOnly} child-null=${undefChildOnly} both-null=${undefBoth};` +
    ` X-mark=${undefX} O-mark=${undefO})`
);
console.log(
  `comparisons: X-mark ${comparisonsX} (violations ${violationsX}),` +
    ` O-mark ${comparisonsO} (violations ${violationsO})`
);
console.log(
  `total checked: ${comparisonsX + comparisonsO + closed + undefParentOnly + undefChildOnly + undefBoth}` +
    ` / ${children}`
);

for (const [title, list] of [
  ["X violations (child diff > parent diff)", exViolationX],
  ["O violations (child diff < parent diff)", exViolationO],
  ["undefined comparisons", exUndef]
]) {
  console.log(`\n${title}:`);
  if (list.length === 0) {
    console.log("  (none in the first examples)");
  }
  for (const e of list) {
    console.log(`  ${e.cause ? e.cause + ": " : ""}${example(e)}`);
  }
}

console.log(
  `\nresult: ${violationsX + violationsO === 0 ? "no counterexample" : violationsX + violationsO + " counterexample(s)"}` +
    `, ${undefParentOnly + undefChildOnly + undefBoth} undefined`
);
