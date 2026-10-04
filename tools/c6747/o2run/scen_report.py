#!/usr/bin/env python3
"""scen_report.py <local dir> <box>:<round dir>... (e.g. tests/out-o2run/scen windows:r1 linux:r1) - the two scenarios, every round's successful runs averaged.
Scenario 1: per program, cpp11 -O2 (test lane) and cl6x (reference lane) cycle.Total on the CCS 5.5 simulator;
a run counts when it stopped at C$$EXIT and printed what the reference printed (else its expected output).
Scenario 2: per kernel ms and Compiler++ ms, cpp11 -O2 against the native compiler; a run counts when it
exited 0 with the reference's checksum (bench) or the reference's output (Compiler++, line endings aside)."""
import os, re, sys, glob, statistics as st

def rd(p):
    try: return open(p, 'rb').read().decode('latin-1')
    except OSError: return None
def norm(s): return None if s is None else '\n'.join(l.rstrip() for l in s.replace('\r', '').split('\n')).rstrip('\n')
def avg(v): return sum(v) / len(v) if v else None
def f(x, d=0): return '-' if x is None else (f'{x:,.{d}f}')

base = sys.argv[1]
INDEX = []
out = []; P = out.append
boxes = {}
for a in sys.argv[2:]:
    box, rnd = a.split(':'); boxes.setdefault(box, []).append(os.path.join(base, box, rnd))

for box, rounds in boxes.items():
    P(f'\n## {box} - {len(rounds)} round(s)\n')
    # ---- scenario 1
    runs = {}   # (prog, tag) -> list of (count, out)
    exp = {}
    for R in rounds:
        for r in glob.glob(os.path.join(R, 'b', '*', '*', 'sim.r*.result')):
            tag = os.path.basename(os.path.dirname(r)); n = os.path.basename(os.path.dirname(os.path.dirname(r)))
            s = rd(r) or ''; m = re.search(r'count=(\d+)', s); pe = re.search(r'pc=(\S+) exit=(\S+)', s)
            ok = m and pe and pe.group(1) == pe.group(2) and 'timeout' not in s and 'failed' not in s
            runs.setdefault((n, tag), []).append((int(m.group(1)) if ok else None, norm(rd(r[:-7] + '.cio'))))
            exp.setdefault(n, norm(rd(os.path.join(R, 'progs', n, 'expected'))))
    tags = sorted({t for _, t in runs if t != 'cpp11-O2'})
    P('### Scenario 1: cpp11 + ASM6x + LNK6x -O2 against cl6x -O2, CCS 5.5 C6747 simulator, cycle.Total\n')
    P('| program | test runs ok | cpp11 -O2 mean | spread | ' + ' | '.join(f'ref {t} runs ok | ref {t} mean | cpp11 / {t}' for t in tags) + ' |')
    P('|---|---|---|---|' + '---|---|---|' * len(tags))
    planned = ok_total = 0; geo = {t: [] for t in tags}; sums = {t: [0, 0, 0] for t in tags}
    for n in sorted({n for n, _ in runs}):
        T = runs.get((n, 'cpp11-O2'), [])
        refs = {t: [c for c, o in runs.get((n, t), []) if c is not None] for t in tags}
        refout = next((o for t in tags for c, o in runs.get((n, t), []) if c is not None), None) or exp.get(n)
        good = [c for c, o in T if c is not None and o == refout]
        planned += len(T); ok_total += len(good)
        mean = avg(good); spread = (max(good) - min(good)) if good else None
        cells = []
        for t in tags:
            rm = avg(refs[t])
            cells += [f'{len(refs[t])}/{len(runs.get((n, t), []))}', f(rm), f'{mean / rm:.3f}' if mean and rm else '-']
            if mean and rm: geo[t].append(mean / rm); sums[t][0] += mean; sums[t][1] += rm; sums[t][2] += 1
        P(f'| {n} | {len(good)}/{len(T)} | {f(mean)} | {f(spread)} | ' + ' | '.join(cells) + ' |')
    for t in tags:
        g = geo[t]
        if g:
            import math
            P(f'\ncpp11 -O2 / cl6x {t}: geometric mean {math.exp(sum(map(math.log, g)) / len(g)):.3f} over {len(g)} programs')
    P(f'\nTest runs counted: {ok_total} of {planned} (a run that failed, did not stop, or printed differently is left out).')
    for t in tags:
        a, b, k = sums[t]
        if b: INDEX.append((box, f'tms6747: cpp11 -O2 / cl6x {"7.4.4 (CCS 5.5)" if t == "744" else "8.2.2 (CCS 7.4)"}', k, a, b, a / b))
    # ---- scenario 2
    P(f'\n### Scenario 2: native -O2, cpp11 against {"cl" if box == "windows" else "g++"}, wall-clock ms\n')
    kern = {}; cppms = {'t': [], 'r': []}; okc = {'t': 0, 'r': 0}; tot = {'t': 0, 'r': 0}
    for R in rounds:
        N = os.path.join(R, 'native')
        refcheck = None; refcpp = None
        for i in (1, 2):
            b = (rd(os.path.join(N, f'run.r.{i}.bench')) or '').replace('\r', '')
            if 'rc=0' in b and refcheck is None: refcheck = re.search(r'check .*', b).group(0)
            c = rd(os.path.join(N, f'run.r.{i}.cpp'))
            if c and refcpp is None: refcpp = norm(c)
        for k in ('t', 'r'):
            for p in glob.glob(os.path.join(N, f'run.{k}.*.bench')):
                tot[k] += 1
                b = (rd(p) or '').replace('\r', ''); cm = re.search(r'check .*', b)
                good = 'rc=0' in b and cm and cm.group(0) == refcheck
                c = norm(rd(p[:-6] + '.cpp')); goodc = c == refcpp and c is not None and 'rc=0' in c
                if good:
                    okc[k] += 1
                    for name, ms in re.findall(r'^(\w+) (\d+)$', b, re.M): kern.setdefault((k, name), []).append(int(ms))
                ms = rd(p[:-6] + '.cppms')
                if goodc and ms and ms.strip().isdigit(): cppms[k].append(int(ms))
    P(f'Runs counted: test {okc["t"]} of {tot["t"]}, reference {okc["r"]} of {tot["r"]}.\n')
    P('| kernel | cpp11 -O2 mean ms | reference mean ms | cpp11 / reference |')
    P('|---|---|---|---|')
    for name in ('fib', 'sieve', 'matmul', 'isort', 'hash', 'virtual', 'total'):
        a, b = avg(kern.get(('t', name), [])), avg(kern.get(('r', name), []))
        P(f'| {name} | {f(a, 1)} | {f(b, 1)} | {a / b:.2f} |' if a and b else f'| {name} | {f(a, 1)} | {f(b, 1)} | - |')
    nat = 'cl /O2' if box == 'windows' else 'g++ -O2'
    kt, kr = avg(kern.get(('t', 'total'), [])), avg(kern.get(('r', 'total'), []))
    ct, cr = avg(cppms['t']), avg(cppms['r'])
    if kt and kr: INDEX.append((box, f'native: cpp11 -O2 / {nat}, six kernels', okc['t'], kt, kr, kt / kr))
    if ct and cr: INDEX.append((box, f'native: cpp11 -O2 / {nat}, Compiler++', len(cppms['t']), ct, cr, ct / cr))
    if kt and kr and ct and cr: INDEX.append((box, f'native: cpp11 -O2 / {nat}, both', okc['t'], kt + ct, kr + cr, (kt + ct) / (kr + cr)))
    a, b = avg(cppms['t']), avg(cppms['r'])
    P(f'| Compiler++ compiling 10 files | {f(a, 0)} ({len(cppms["t"])} runs) | {f(b, 0)} ({len(cppms["r"])} runs) | ' +
      (f'{a / b:.2f} |' if a and b else '- |'))
P('\n## Index: test-run average / reference-run average\n')
P('| box | comparison | measured | test average | reference average | **index** |')
P('|---|---|---|---|---|---|')
for box, what, k, a, b, x in INDEX:
    unit = 'cycles (sum of means)' if what.startswith('tms') else 'ms'
    P(f'| {box} | {what} | {k} {"programs" if what.startswith("tms") else "runs"} | {a:,.0f} | {b:,.0f} | **{x:.3f}** |')
P('\nBelow 1 cpp11 is faster. tms6747 averages are cycle.Total sums over the programs both lanes measured.')
print('\n'.join(out))
