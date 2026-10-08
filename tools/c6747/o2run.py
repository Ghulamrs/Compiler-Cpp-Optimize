#!/usr/bin/env python3
"""o2run.py - the routine cpp11 -O2 exercise on the two C6747 boxes, from the Mac (control room only).

    tools/c6747/o2run.py init      plan, package, ship and launch on both boxes; prints the run's stamp
    tools/c6747/o2run.py status    where each box is (progress.log), for the last run or STAMP=<stamp>
    tools/c6747/o2run.py collect   fetch each box's results into tests/out-o2run/<stamp>/<box>
    tools/c6747/o2run.py report    the tables, against the stored cl6x reference (o2run/reference-<box>.json)

    tools/c6747/o2run.py no-ti-build [dir]   the cases cl6x 7.4.4 could not build in a collected run
                                             (default the last), each with its first error, and the
                                             cases tms6747 does not run at all, each with its reason

The plan (agreed 04-10-2026): the tools are the RIDE 4.7 installed on each box; cl6x 7.4.4 on the CCS 5.5
simulator is the oracle. The 16 lambda cases were exempt until the review of 2026-10-08 (D10): they run
now, and where cl6x cannot build one - it is C++03 - clang's recorded output is the stand-in.
  Windows: cpp11 -O2 on everything - every case, kernel, program, example, the compilerpp project and
           all ten Compiler++ harness files; cpp11 -O1, cl6x 7.4.4 -O2 and cl6x 8.2.2 -O2 on everything
           but the two Compiler++ programs (TI's builds of those take 13 minutes and do not change).
           20 build jobs, 12 simulator lanes started 10 s apart, 80 runs a JVM.
  Linux:   cpp11 -O2 on every case (LINUX_CASES=fifth: every fifth and the cases that failed on 04-10),
           the kernels, programs and examples (not the compilerpp project), and harness files 0 and 5.
           2 build jobs, 2 simulator lanes started 30 s apart, 40 runs a JVM.
Predicted: Windows 15 min (14-16.5); Linux 15.5 min (14.5-17) with every case, one JVM start at a time.
Measured 1004-195852: Windows 19 min 24 s, Linux 17 min 44 s - 245 and 76 runs lost to starts colliding.
Measured 04-10-2026 (1004-192737): Linux 12 min 26 s with a fifth.
"""
import glob, hashlib, json, math, os, re, shutil, subprocess, sys, tarfile, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
KIT = os.path.join(HERE, 'o2run')
OUT = os.path.join(ROOT, 'tests', 'out-o2run')
KEY = os.path.expanduser('~/Documents/Claude/myMorningWalk.pem')
BOXES = {
    'windows': {'ssh': ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=20', 'windows'],
                'scp': ['scp', '-q', '-o', 'BatchMode=yes'], 'host': 'windows',
                'dir': 'C:/cxx1/o2run/{stamp}', 'predict': (15.0, 16.0, 17.0)},
    'linux':   {'ssh': ['ssh', '-i', KEY, '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=20', 'ec2-user@52.202.164.123'],
                'scp': ['scp', '-q', '-i', KEY, '-o', 'BatchMode=yes'], 'host': 'ec2-user@52.202.164.123',
                'dir': 'o2run/{stamp}', 'predict': (15.0, 16.0, 17.0)},
}
# The cases that failed on TI's simulator on 04-10-2026: Linux runs them whatever the sampling picks.
FAILURE_LIST = ('array-destructor-only array-static-storage dynamic-init dynamic-init-const dynamic-init-reference '
                'dynamic-init-scalar global-constructor static-base-pointer template-static-data-member-ctor '
                'throw-pointer throw-class-pointer-ms narrowing-allowed template-nontype-numeric').split()
EXAMPLES = ['ex-sample', 'ex-sampleext', 'ex-hello-ccs', 'ex-cpp-inventory', 'ex-cpp-shapes', 'ex-cpp-table',
            'ex-cpp-vector3', 'ex-prog-hello', 'ex-prog-smart', 'ex-prog-words', 'ex-compilerpp']
BIG = {'compilerpp-harness', 'ex-compilerpp'}


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


# ---- the programs -------------------------------------------------------------------------------
def stage_programs(dst):
    """Every program as a directory: its sources, meta and expected output. Examples are made on the box."""
    names = []
    def mk(name, meta, files, expected=None):
        d = os.path.join(dst, name); os.makedirs(d, exist_ok=True)
        for src, as_ in files: shutil.copy(src, os.path.join(d, as_))
        if expected: shutil.copy(expected, os.path.join(d, 'expected'))
        with open(os.path.join(d, 'meta'), 'w') as f: f.write(''.join(f'{k}={v}\n' for k, v in meta.items()))
        names.append(name)
    skip = set()
    for lst in ('tests/tms6747-lp64.txt', 'tests/tms6747-exceptions.txt'):
        for l in open(os.path.join(ROOT, lst)):
            if l.strip() and not l.startswith('#'): skip.add(l.split()[0])
    headers = [(h, os.path.basename(h)) for h in glob.glob(os.path.join(ROOT, 'tests/cases/*.h'))]
    for src in sorted(glob.glob(os.path.join(ROOT, 'tests/cases/*.cpp'))):
        b = os.path.basename(src)[:-4]; base = src[:-4]
        if os.path.exists(base + '.error') or b in skip: continue
        if os.path.exists(base + '.notarget') and re.search(r'^tms6747\s', open(base + '.notarget').read(), re.M): continue
        mk('case-' + b, dict(SET='cases', MAP='ddr', TMO='120000', SRCS=b + '.cpp'), [(src, b + '.cpp')] + headers, base + '.expected')
    for d, pre in (('tools/c6747/bench', 'kern'), ('tools/c6747/programs', 'prog'), ('tools/c6747/ctor', 'prog')):
        for src in sorted(glob.glob(os.path.join(ROOT, d, '*.c')) + glob.glob(os.path.join(ROOT, d, '*.cpp'))):
            f = os.path.basename(src); b = f.rsplit('.', 1)[0]
            mk(f'{pre}-{b}', dict(SET='kernels' if pre == 'kern' else 'programs', MAP='shram', TMO='1800000', SRCS=f),
               [(src, f)], os.path.join(ROOT, d, b + '.expected'))
    h = os.path.join(dst, 'compilerpp-harness'); os.makedirs(h)
    for f in glob.glob(os.path.join(KIT, 'harness', '*')): shutil.copy(f, h)
    with open(os.path.join(h, 'meta'), 'w') as f:
        f.write('SET=compilerpp\nMAP=ddr\nTMO=1800000\nSRCS=harness.cpp\nSIMFILES=10\n')
    names.append('compilerpp-harness')
    return names + EXAMPLES


def plans(names):
    cases = sorted(n for n in names if n.startswith('case-'))
    rest = [n for n in names if not n.startswith('case-')]
    win_b, win_s = [], []
    for n in names:
        tags = ['cpp11-O2'] if n in BIG else ['cpp11-O2', 'cpp11-O1', '744', '822']
        for t in tags:
            win_b.append(f'{n} {t}')
            if n == 'compilerpp-harness': win_s += [f'{n} {t} harnessFile={k}' for k in range(10)]
            else: win_s.append(f'{n} {t}')
    # LINUX_CASES=all (the default since 04-10-2026: +3.5 min) or fifth: every fifth case and the failure list.
    if os.environ.get('LINUX_CASES', 'all') == 'fifth':
        pick = set(cases[::5]) | {'case-' + c for c in FAILURE_LIST if 'case-' + c in cases}
    else:
        pick = set(cases)
    lin = sorted(pick) + [n for n in rest if n != 'ex-compilerpp']
    lin_b = [f'{n} cpp11-O2' for n in lin]
    lin_s = []
    for n in lin:
        if n == 'compilerpp-harness': lin_s += [f'{n} cpp11-O2 harnessFile={k}' for k in (0, 5)]
        else: lin_s.append(f'{n} cpp11-O2')
    return {'windows': (win_b, win_s), 'linux': (lin_b, lin_s)}


# ---- init ---------------------------------------------------------------------------------------
def init():
    stamp = time.strftime('%m%d-%H%M%S')
    work = tempfile.mkdtemp(prefix='o2run.')
    kit = os.path.join(work, stamp); os.makedirs(kit)
    names = stage_programs(os.path.join(kit, 'progs'))
    for f in ('o2run.sh', 'collect.sh', 'runbatch.js', 'C6747.cmd', 'C6747-ddr.cmd', 'c6747ca-windows.ccxml',
              'c6747ca-linux.ccxml', 'start-detached.ps1'):
        shutil.copy(os.path.join(KIT, f), kit)
    shutil.copytree(os.path.join(KIT, 'expected-ex'), os.path.join(kit, 'expected-ex'))
    P = plans(names)
    os.makedirs(os.path.join(OUT, stamp), exist_ok=True)
    for box, (b, s) in P.items():
        bk = os.path.join(work, box, stamp)
        shutil.copytree(kit, bk)
        s = [simline(kit, box, stamp, l) for l in s]
        for fn, lines in (('plan-build.txt', b), ('plan-sim.txt', s)):
            with open(os.path.join(bk, fn), 'w', newline='\n') as f: f.write('\n'.join(lines) + '\n')
        tgz = os.path.join(work, f'{box}.tgz')
        with tarfile.open(tgz, 'w:gz') as t: t.add(bk, arcname=stamp)
        print(f'{box}: {len(b)} builds, {len(s)} simulator runs')
    launched = {}
    for box, B in BOXES.items():
        d = B['dir'].format(stamp=stamp); tgz = os.path.join(work, f'{box}.tgz')
        if box == 'windows':
            wd = d.replace('/', '\\')
            run(B['ssh'] + [f'mkdir C:\\cxx1\\o2run 2>nul & exit 0'])
            r = run(B['scp'] + [tgz, f'{B["host"]}:C:/cxx1/o2run/{stamp}.tgz'])
            if r.returncode: sys.exit(f'windows: cannot ship: {r.stderr}')
            run(B['ssh'] + [f'cd /d C:\\cxx1\\o2run && tar -xzf {stamp}.tgz && del {stamp}.tgz'])
            cmd = os.path.join(work, 'run.cmd')
            with open(cmd, 'w', newline='\r\n') as f:
                f.write(f'@echo off\n"C:\\Program Files\\Git\\bin\\bash.exe" {d}/o2run.sh windows > {wd}\\run.log 2>&1\n')
            run(B['scp'] + [cmd, f'{B["host"]}:{d}/run.cmd'])
            r = run(B['ssh'] + [f'powershell -NoProfile -ExecutionPolicy Bypass -File {wd}\\start-detached.ps1 {wd}\\run.cmd'])
        else:
            run(B['ssh'] + ['mkdir -p o2run'])
            r = run(B['scp'] + [tgz, f'{B["host"]}:o2run/{stamp}.tgz'])
            if r.returncode: sys.exit(f'linux: cannot ship: {r.stderr}')
            r = run(B['ssh'] + [f'cd o2run && tar xzf {stamp}.tgz && rm {stamp}.tgz && cd {stamp} && chmod +x *.sh && '
                                f'(nohup setsid ./o2run.sh linux > run.log 2>&1 < /dev/null &)'])
        launched[box] = time.time()
        print(f'{box}: launched in {d} ({r.returncode})')
    json.dump({'stamp': stamp, 'launched': launched}, open(os.path.join(OUT, 'last.json'), 'w'))
    shutil.rmtree(work)
    print(f'stamp {stamp}')


def simline(kit, box, stamp, line):
    """<program> <build> [harnessFile=k] -> the simulator's list line, absolute paths on that box."""
    n, tag, *k = line.split()
    meta = dict(l.rstrip('\n').split('=', 1) for l in open(os.path.join(kit, 'progs', n, 'meta')) if '=' in l) \
        if os.path.exists(os.path.join(kit, 'progs', n, 'meta')) else {'TMO': '600000'}
    root = f'C:/cxx1/o2run/{stamp}' if box == 'windows' else f'/home/ec2-user/o2run/{stamp}'
    b = f'{root}/b/{n}/{tag}'
    if k:
        f = k[0].split('=')[1]
        return f'{b}/image.out {b}/sim.{f}.cio {b}/sim.{f}.result {meta.get("TMO", "1800000")} {k[0]}'
    if n == 'ex-compilerpp':
        return f'{b}/image.out {b}/sim.cio {b}/sim.result 600000 -- prog {root}/progs/ex-compilerpp/input-sample.cpp'
    return f'{b}/image.out {b}/sim.cio {b}/sim.result {meta.get("TMO", "600000")}'


def last():
    return os.environ.get('STAMP') or json.load(open(os.path.join(OUT, 'last.json')))['stamp']


def status():
    stamp = last()
    for box, B in BOXES.items():
        d = B['dir'].format(stamp=stamp)
        cmd = (f'type {d.replace("/", chr(92))}\\progress.log' if box == 'windows' else f'cat {d}/progress.log')
        r = run(B['ssh'] + [cmd])
        print(f'== {box} {stamp}\n{r.stdout.strip() or r.stderr.strip()}')


def collect():
    stamp = last()
    for box, B in BOXES.items():
        d = B['dir'].format(stamp=stamp)
        c = (f'"C:\\Program Files\\Git\\bin\\bash.exe" {d}/collect.sh' if box == 'windows' else f'bash {d}/collect.sh')
        run(B['ssh'] + [c])
        dst = os.path.join(OUT, stamp, box); shutil.rmtree(dst, ignore_errors=True); os.makedirs(dst)
        r = run(B['scp'] + [f'{B["host"]}:{d}/results.tgz', dst])
        if r.returncode: print(f'{box}: nothing to fetch yet ({r.stderr.strip()})'); continue
        with tarfile.open(os.path.join(dst, 'results.tgz')) as t: t.extractall(dst)
        print(f'{box}: collected into {dst}')


# ---- report -------------------------------------------------------------------------------------
def rd(p):
    try: return open(p, 'rb').read().decode('latin-1')
    except OSError: return None
def norm(s):
    if s is None: return None
    return '\n'.join(l.rstrip() for l in s.replace('\r', '').split('\n')).rstrip('\n')
def h(s): return hashlib.md5((s or '').encode('latin-1')).hexdigest()
def codebytes(files):
    import struct
    tot = 0
    for f in files:
        d = open(f, 'rb').read()
        if d[:4] != b'\x7fELF': continue
        shoff, = struct.unpack_from('<I', d, 0x20); es, n = struct.unpack_from('<HH', d, 0x2e)
        for i in range(n):
            s = struct.unpack_from('<IIIIIIIIII', d, shoff + i * es)
            if s[1] == 1 and s[2] & 4: tot += s[5]
    return tot or None

def load(box_dir):
    """{(prog, tag, file): {'count', 'stopped', 'out', 'exp'}} and per-build facts."""
    runs, builds = {}, {}
    for st in glob.glob(os.path.join(box_dir, 'b', '*', '*', 'status')):
        b = os.path.dirname(st); tag = os.path.basename(b); n = os.path.basename(os.path.dirname(b))
        p = os.path.join(box_dir, 'progs', n)
        objs = glob.glob(os.path.join(b, '*.obj')) + glob.glob(os.path.join(b, 'obj', '*.obj'))
        builds[(n, tag)] = {'status': rd(st).strip(), 'csec': float((rd(os.path.join(b, 'compile.sec')) or '0') or 0),
                            'code': codebytes(objs), 'vm_ok': None}
        vm = rd(os.path.join(b, 'vm.txt'))
        if vm is not None: builds[(n, tag)]['vm_ok'] = norm(vm) == norm(rd(os.path.join(p, 'expected')))
        for r in glob.glob(os.path.join(b, 'sim*.result')):
            k = re.sub(r'^sim\.?|\.?result$', '', os.path.basename(r)) or 'None'
            s = rd(r); m = re.search(r'count=(\d+)', s); pe = re.search(r'pc=(\S+) exit=(\S+)', s)
            out = norm(rd(r[:-len('.result')] + '.cio'))
            exp = norm(rd(os.path.join(p, f'expected.{k}' if k != 'None' else 'expected')))
            if 'RESULT failed' in s or not m: continue        # unmeasured: never counted, listed apart
            runs[(n, tag, k)] = {'count': int(m.group(1)) if m else None, 'failed': False,
                                 'stopped': bool(pe and pe.group(1) == pe.group(2)) and 'timeout' not in s,
                                 'out': out, 'exp': exp}
    return runs, builds

def planned(box_dir):
    """Every run the plan asked for, as (program, build, file), from plan-sim.txt's result paths."""
    keys = []
    for l in (rd(os.path.join(box_dir, 'plan-sim.txt')) or '').splitlines():
        if not l.strip(): continue
        m = re.search(r'/b/([^/]+)/([^/]+)/sim\.?(\d*)\.result', l.split()[2])
        if m: keys.append((m.group(1), m.group(2), m.group(3) or 'None'))
    return keys

def phases(box_dir):
    log = rd(os.path.join(box_dir, 'progress.log')) or ''
    t = [(l[:8], l[9:]) for l in log.splitlines() if re.match(r'\d\d:\d\d:\d\d ', l)]
    sec = lambda s: int(s[:2]) * 3600 + int(s[3:5]) * 60 + int(s[6:8])
    marks = {}
    for ts, what in t:
        m = re.match(r'phase (\w+)|done', what)
        if m: marks[m.group(1) or 'done'] = sec(ts)
    out = {}
    order = ['build', 'vm', 'sim', 'done']
    for a, b in zip(order, order[1:]):
        if a in marks and b in marks: out[a] = (marks[b] - marks[a]) % 86400
    if 'info' in marks and 'done' in marks: out['total'] = (marks['done'] - marks['info']) % 86400
    return out, t

def report():
    stamp = last(); base = os.path.join(OUT, stamp); lines = []; P = lines.append
    mm = lambda s: f'{s // 60} min {s % 60:02d} s'
    P(f'# o2run {stamp}\n')
    P('| | predicted | measured | build | VM6747 | simulator | verdict |')
    P('|---|---|---|---|---|---|---|')
    data = {}
    for box, B in BOXES.items():
        d = os.path.join(base, box)
        if not os.path.isdir(d): P(f'| {box} | | not collected | | | | |'); continue
        ph, _ = phases(d); data[box] = load(d)
        lo, mid, hi = B['predict']; tot = ph.get('total')
        v = '-' if tot is None else ('inside' if lo * 60 <= tot <= hi * 60 else ('UNDER' if tot < lo * 60 else 'OVER'))
        P(f'| {box} | {mid} min ({lo}-{hi}) | {mm(tot) if tot else "-"} | {mm(ph["build"]) if "build" in ph else "-"} | '
          f'{mm(ph["vm"]) if "vm" in ph else "-"} | {mm(ph["sim"]) if "sim" in ph else "-"} | {v} |')
    for box, (runs, builds) in data.items():
        ref = json.load(open(os.path.join(KIT, f'reference-{box}.json')))
        P(f'\n## {box}\n')
        plan = planned(os.path.join(base, box))
        built = [k for k in plan if (builds.get((k[0], k[1])) or {}).get('status') == 'ok']
        unmeasured = sorted(set(built) - set(runs))
        P(f'Planned {len(plan)} simulator runs; {len(plan) - len(built)} have no image (their build failed: '
          f'cpp11 refuses structs.c as C++, cl6x the C++11 cases); of the {len(built)} built, **{len(runs)} measured** and '
          f'**{len(unmeasured)} unmeasured**, which are left out of every count below.\n')
        if unmeasured:
            P('Unmeasured (no complete result after the retries): ' +
              ', '.join(f'{n} {t}' + ('' if k == 'None' else f' file {k}') for n, t, k in unmeasured) + '\n')
        lf = (rd(os.path.join(base, box, 'lanes', 'lanes-failed.txt')) or '').strip()
        if lf: P(f'Lanes that exited non-zero: {lf}\n')
        # correctness of cpp11 -O2 against the oracle: this run's 7.4.4 where it ran, else the stored one, else expected
        cnt = {'agree with 7.4.4': [], 'agree with expected (7.4.4 cannot build)': [], 'differ': [], 'did not stop': []}
        for (n, tag, k), r in sorted(runs.items()):
            if tag != 'cpp11-O2': continue
            o = runs.get((n, '744', k)); so = ref.get(f'{n}|744|{k}')
            if not r['stopped']: cnt['did not stop'].append(n); continue
            if o and o['stopped']: ok, w = r['out'] == o['out'], 'agree with 7.4.4'
            elif so and so['stopped']: ok, w = h(r['out']) == so['out'], 'agree with 7.4.4'
            else: ok, w = r['out'] == r['exp'], 'agree with expected (7.4.4 cannot build)'
            if ok: cnt[w].append(n)
            elif r['out'] == r['exp']: cnt['agree with expected (7.4.4 cannot build)'].append(n + ' (7.4.4 differs from clang)')
            else: cnt['differ'].append(f'{n}' + ('' if k == 'None' else f' file {k}'))
        P('**cpp11 -O2 on the CCS 5.5 simulator, against the oracle**\n')
        for k, v in cnt.items():
            P(f'- {k}: {len(v)}' + (': ' + ', '.join(sorted(set(v))) if k in ('differ', 'did not stop') and v else ''))
        vm = [b['vm_ok'] for (n, t), b in builds.items() if t == 'cpp11-O2' and b['vm_ok'] is not None]
        P(f'- VM6747: {sum(vm)} of {len(vm)} print their expected output')
        # drift: this run against the stored reference
        P('\n**Against the stored reference (04-10-2026)**\n')
        P('| build | runs compared | same output | same cycles | output changed |')
        P('|---|---|---|---|---|')
        for tag in ('cpp11-O2', '744', '822'):
            same_o = same_c = tot = 0; changed = []
            for (n, t, k), r in runs.items():
                if t != tag: continue
                s = ref.get(f'{n}|{tag}|{k}')
                if not s or not r['stopped'] or not s['stopped']: continue
                tot += 1; so = h(r['out']) == s['out']; same_o += so; same_c += r['count'] == s['count']
                if not so: changed.append(n)
            if tot: P(f'| {tag} | {tot} | {same_o} | {same_c} | {", ".join(sorted(changed)) or "-"} |')
        # speed
        def ratio(a_tag, b_tag, only=None):
            l, sa, sb, n_ = [], 0, 0, 0
            for (n, t, k), r in runs.items():
                if t != a_tag or (only and not only(n)): continue
                o = runs.get((n, b_tag, k))
                cb = o['count'] if o and o['stopped'] else (ref.get(f'{n}|{b_tag}|{k}') or {}).get('count')
                if r['stopped'] and r['count'] and cb and (not o or r['out'] == o['out']):
                    l.append(math.log(r['count'] / cb)); sa += r['count']; sb += cb; n_ += 1
            return (n_, math.exp(sum(l) / n_) if n_ else None, sa / sb if sb else None)
        P('\n**Cycles (cycle.Total)**\n')
        P('| comparison | programs | geometric mean | sum |')
        P('|---|---|---|---|')
        for a, b, lab, only in (('cpp11-O2', '744', 'cpp11 -O2 / cl6x 7.4.4, test cases', lambda n: n.startswith('case-')),
                                ('cpp11-O2', '744', 'cpp11 -O2 / cl6x 7.4.4, kernels, programs, examples', lambda n: not n.startswith('case-') and n not in BIG),
                                ('cpp11-O2', '744', 'cpp11 -O2 / cl6x 7.4.4, Compiler++ harness (per file)', lambda n: n == 'compilerpp-harness'),
                                ('cpp11-O2', '822', 'cpp11 -O2 / cl6x 8.2.2, Compiler++ harness (per file)', lambda n: n == 'compilerpp-harness'),
                                ('cpp11-O2', 'cpp11-O1', 'cpp11 -O2 / cpp11 -O1, everything both ran', None)):
            n_, g, s = ratio(a, b, only)
            if n_: P(f'| {lab} | {n_} | {g:.3f} | {s:.3f} |')
        for (n, tag, k), r in sorted(runs.items()):
            if n in ('ex-sampleext', 'ex-sample') and tag == 'cpp11-O2':
                o = runs.get((n, '744', k)) or {}; so = ref.get(f'{n}|744|{k}') or {}
                c7 = o.get('count') or so.get('count')
                P(f'\n{n}: cpp11 -O2 {r["count"]:,} cycles, cl6x 7.4.4 {c7:,}, ratio {r["count"] / c7:.2f}' if c7 and r['count'] else f'\n{n}: -')
    text = '\n'.join(lines)
    open(os.path.join(base, 'report.md'), 'w').write(text + '\n')
    print(text)


def no_ti_build():
    """Which cases have no TI build and why: cl6x 7.4.4's first error, and the cases tms6747 skips."""
    d = sys.argv[2] if len(sys.argv) > 2 else os.path.join(OUT, last()['stamp'])
    b = os.path.join(d, 'windows', 'b')
    rows = []
    for c in sorted(glob.glob(os.path.join(b, 'case-*'))):
        st = rd(os.path.join(c, '744', 'status')).strip()
        if st and st != 'ok':
            m = re.search(r', line \d+: error[^:]*: *(.*)', rd(os.path.join(c, '744', 'build.log')))
            rows.append(f'{os.path.basename(c)[5:]}\t{m.group(1)[:100] if m else st}')
    print(f'# {len(rows)} cases cl6x 7.4.4 (C++03) did not build in {os.path.basename(os.path.normpath(d))}; '
          'cpp11 -O2 is held to the case\'s recorded output (clang\'s) there')
    print('\n'.join(rows))
    print('\n# cases tms6747 does not run at all, with the reason each gives')
    for src in sorted(glob.glob(os.path.join(ROOT, 'tests/cases/*.notarget'))):
        m = re.search(r'^tms6747\s+(.*)', open(src).read(), re.M)
        if m: print(f'{os.path.basename(src)[:-9]}\t.notarget: {m.group(1)[:100]}')
    for lst in ('tests/tms6747-lp64.txt', 'tests/tms6747-exceptions.txt'):
        for l in open(os.path.join(ROOT, lst)):
            if l.strip() and not l.startswith('#'):
                n, _, why = l.strip().partition(' ')
                print(f'{n}\t{os.path.basename(lst)}: {why.strip()[:100]}')


if __name__ == '__main__':
    {'init': init, 'status': status, 'collect': collect, 'report': report, 'no-ti-build': no_ti_build}.get(
        sys.argv[1] if len(sys.argv) > 1 else '', lambda: sys.exit(__doc__))()
