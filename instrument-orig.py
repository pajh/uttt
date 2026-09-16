from pathlib import Path
source = Path('src/legacy/orig.c').read_text()
start = source.index('Pos evaluateMovesMM(')
end = source.index('\n}', start) + 2
section = source[start:end]
needle = '            return timeout;'
assert section.count(needle) == 2
section = section.replace(needle, '            if(getenv("CG_LOCAL_HELLO")) printf("@DFS_FAILED\\t%d\\t%d\\t%s\\n",spaces_left,valid_moves->count,spaces_left<=18?"primary":"narrow");\n' + needle)
Path('bin/orig_instrumented.h').write_text(source[:start] + section + source[end:])
