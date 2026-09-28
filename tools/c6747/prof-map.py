#!/usr/bin/env python3
"""prof-map.py <program.out> <prof.js output> [top] [--map <link map>]

Maps prof.js's SAMPLE lines to the functions of a TI ELF image, by its symbol table, and prints
the functions that took the most samples, each with its share, demangled when c++filt can.

With --map, also the split: every .text piece in the link map belongs to the program's own
objects or to a library member (a line naming a .lib, or continuing one), and each sample is
charged to one or the other by its address. Printed as "SPLIT program=<pct> library=<pct>" -
the program's share being the code the compiler under test generated, templates included."""
import collections, struct, subprocess, sys


def functions(path):
    d = open(path, 'rb').read()
    shoff, = struct.unpack_from('<I', d, 0x20)
    shes, shn, _ = struct.unpack_from('<HHH', d, 0x2e)
    hs = [struct.unpack_from('<IIIIIIIIII', d, shoff + i * shes) for i in range(shn)]
    out = []
    for h in hs:
        if h[1] != 2: continue
        st = hs[h[6]]
        for i in range(h[5] // 16):
            nm, val, size, info, other, shndx = struct.unpack_from('<IIIBBH', d, h[4] + i * 16)
            if info & 15 != 2 or shndx == 0: continue          # defined functions
            if (info >> 4) == 0: continue                        # locals: TI types its $C$L labels as functions
            o = st[4] + nm
            out.append((val, d[o:d.index(b'\0', o)].decode()))
    return sorted(set(out))


def text_owners(path):
    """(start, end, 'program' | 'library') for every piece of .text in a TI link map."""
    import re
    out, on, lib = [], False, False
    for line in open(path, errors='replace'):
        if re.match(r'^\.text\s', line): on = True; continue
        if on and re.match(r'^\S', line): break
        m = re.match(r'^\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+(.*)$', line) if on else None
        if not m or 'HOLE' in m.group(3): continue
        rest = m.group(3)
        if '.lib' in rest: lib = True
        elif not rest.startswith(':'): lib = False       # a line of its own: an object's piece
        a, n = int(m.group(1), 16), int(m.group(2), 16)
        out.append((a, a + n, 'library' if lib else 'program'))
    return out


def main():
    args = sys.argv[1:]
    mapfile = None
    if '--map' in args:
        i = args.index('--map'); mapfile = args[i + 1]; del args[i:i + 2]
    sys.argv[1:] = args
    fns = functions(sys.argv[1])
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    addrs = [a for a, _ in fns]
    counts = collections.Counter()
    total = 0
    import bisect
    for line in open(sys.argv[2]):
        if not line.startswith('SAMPLE '): continue
        pc = int(line.split()[1], 16)
        i = bisect.bisect_right(addrs, pc) - 1
        counts[fns[i][1] if i >= 0 else '?'] += 1
        total += 1
    if mapfile:
        owners = text_owners(mapfile)
        split = collections.Counter()
        for line in open(sys.argv[2]):
            if not line.startswith('SAMPLE '): continue
            pc = int(line.split()[1], 16)
            split[next((o for a, e, o in owners if a <= pc < e), 'other')] += 1
        n = sum(split.values()) or 1
        print('SPLIT program=%.1f library=%.1f other=%.1f' % (100.0 * split['program'] / n,
              100.0 * split['library'] / n, 100.0 * split['other'] / n))
    names = [n for n, _ in counts.most_common(top)]
    try:
        dem = subprocess.run(['c++filt'], input='\n'.join(names), capture_output=True, text=True).stdout.split('\n')
    except OSError:
        dem = names
    print('%d samples' % total)
    for (n, c), d in zip(counts.most_common(top), dem):
        print('%6.1f%%  %5d  %s' % (100.0 * c / total, c, d or n))


if __name__ == '__main__':
    main()
