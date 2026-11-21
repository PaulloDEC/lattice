#!/usr/bin/env python3
"""Tiny mutation tester: mutate operators/constants on a line range of a C file, run a diff test, report survivors.
usage: mutate.py <src.c> <first_func_marker> <test.py> <nscen> [stride]"""
import sys, re, subprocess, shutil, os
src, marker, test, nscen = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
stride = int(sys.argv[5]) if len(sys.argv) > 5 else 1
orig = open(src).read(); lines = orig.split('\n')
start = next(i for i, l in enumerate(lines) if marker in l)
end = len(lines)
muts = []
subs = [(r'\+ 0x', '- 0x'), (r' \+ ', ' - '), (r' - ', ' + '), (r'==', '!='), (r'!=', '=='), (r' > ', ' >= '), (r' < ', ' <= '), (r' >= ', ' > '), (r' <= ', ' < '),
        (r'\|\|', '&&'), (r'&&', '||'), (r'\b0xF0\b', '0xEF'), (r'\b0xF1\b', '0xF2'), (r'\b0xEF\b', '0xEE'), (r'\b0x4F\b', '0x50'), (r'\b2 \*', '1 *'), (r'\b-3\b', '-4'),
        (r'\b0xFF\b', '0xFE'), (r'\b9 ?\b', '8'), (r'\b0x18\b', '0x19'), (r'\b0xA\b', '0xB'), (r'y \+ 5', 'y + 6'), (r'\b8\b', '9'), (r'\b15\b', '14'), (r'\bx \* 4\b', 'x * 3'),
        (r'\+ 1\b', '+ 2'), (r'\b3 : 1', '2 : 1'), (r'\b3 : 1', '3 : 0'), (r'\b2 : 0', '1 : 0'), (r'> 2', '> 3'), (r'!g_shots.dir', 'g_shots.dir'), (r'c2 = 2 \*', 'c2 = 1 *'), (r'0x1DD', '0x1DE'), (r'0x19', '0x1A'), (r'0x28', '0x27'), (r'0x1E0', '0x1DF'), (r'0x2D0', '0x2CF'), (r'< 8', '< 9'), (r'> 10', '> 11'), (r'= 5;', '= 6;'), (r'0xF\b', '0xE'), (r'\b0x10\b', '0x11'), (r'rem == 0', 'rem != 0'), (r'> 0 &&', '>= 0 &&'), (r' - 1 -', ' - 0 -'), (r'\+ 2\) / 2', '+ 1) / 2'), (r'\+ 1\) / 2', '+ 0) / 2'), (r'= 0xC', '= 0xD'), (r'0xF0\)\)', '0xEF))')]
for i in range(start, end):
    for pat, rep in subs:
        for m in re.finditer(pat, lines[i]):
            new = lines[i][:m.start()] + re.sub(pat, rep, lines[i][m.start():m.end()]) + lines[i][m.end():]
            if new != lines[i]: muts.append((i, new)); break
muts = muts[::stride]
print(len(muts), 'mutants'); surv = []
try:
    for k, (i, new) in enumerate(muts):
        l2 = list(lines); l2[i] = new; open(src, 'w').write('\n'.join(l2))
        try: r = subprocess.run(['python3', test, nscen], capture_output=True, text=True, timeout=int(os.environ.get('MUT_TIMEOUT', 1200)))
        except subprocess.TimeoutExpired: continue          # a hang counts as killed
        out = r.stdout + r.stderr
        if r.returncode == 0 and ' 0 mismatches' in out: surv.append((i + 1, lines[i].strip(), new.strip())); print('SURVIVED', i + 1, lines[i].strip()[:90], '=>', new.strip()[:90], flush=True)
finally:
    open(src, 'w').write(orig)
print(f'{len(muts) - len(surv)}/{len(muts)} killed')
