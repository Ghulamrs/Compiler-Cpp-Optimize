import csv, sys, statistics as st
rows = []
for f in sys.argv[1:]:
    rows += list(csv.DictReader(open(f)))
order = []
for r in rows:
    if r['variant'] not in order: order.append(r['variant'])
print(f"{'variant':14} {'built':>5} {'simEXIT':>7} {'sim=exp':>7} {'ccsRan':>6} {'ccsEXIT':>7} {'ccs=exp':>7} {'sim=ccs':>7} {'exit=':>5}  ratio sim/ccs cycle.CPU (median min max)")
for v in order:
    R = [r for r in rows if r['variant'] == v]; B = [r for r in R if r['build'] == 'ok']
    C = [r for r in B if r['ccsCycles']]
    rat = sorted(float(r['ratio']) for r in C if r['ratio'] and r['simExit'] == 'C$$EXIT' and r['ccsExit'] == 'C$$EXIT')
    ex = sum(1 for r in C if (r['simExit'] == 'C$$EXIT') == (r['ccsExit'] == 'C$$EXIT'))
    print(f"{v:14} {len(B):>5} {sum(r['simExit']=='C$$EXIT' for r in B):>7} {sum(r['simVsExp']=='ok' for r in B):>7} {len(C):>6} "
          f"{sum(r['ccsExit']=='C$$EXIT' for r in C):>7} {sum(r['ccsVsExp']=='ok' for r in C):>7} {sum(r['simVsCcs']=='same' for r in C):>7} {ex:>5}  "
          + (f"{st.median(rat):.4f} {rat[0]:.4f} {rat[-1]:.4f} (n={len(rat)})" if rat else "-"))
print("\nvm6747sim output != CCS output:")
for r in rows:
    if r['simVsCcs'] == 'DIFF': print(f"  {r['prog']:34} {r['variant']:13} sim={r['simVsExp']:6} ccs={r['ccsVsExp']:6} simExit={r['simExit']} ccsExit={r['ccsExit']}")
print("\nexit disagreements (one reached C$$EXIT, the other not):")
for r in rows:
    if r['ccsCycles'] and (r['simExit'] == 'C$$EXIT') != (r['ccsExit'] == 'C$$EXIT'):
        print(f"  {r['prog']:34} {r['variant']:13} simExit={r['simExit']} ccsExit={r['ccsExit']} sim={r['simCycles']} ccs={r['ccsCycles']}")
print("\nCCS (the TI oracle) output != .expected, by variant:")
for v in order:
    L = sorted(r['prog'] for r in rows if r['variant'] == v and r['ccsCycles'] and r['ccsVsExp'] != 'ok')
    print(f"  {v:13} {len(L):3}: {' '.join(L)}")
print("\nCCS not run (no RESULT):")
for v in order:
    L = sorted(r['prog'] for r in rows if r['variant'] == v and r['build'] == 'ok' and not r['ccsCycles'])
    if L: print(f"  {v:13} {len(L):3}: {' '.join(L)}")
print("\nbuild failures, by variant:")
for v in order:
    L = sorted(r['prog'] for r in rows if r['variant'] == v and r['build'] != 'ok')
    print(f"  {v:13} {len(L):3}: {' '.join(L[:60])}{' ...' if len(L) > 60 else ''}")
print("\nratio outliers (outside 0.95..1.02):")
for r in rows:
    if r['ratio'] and r['simExit'] == 'C$$EXIT' and r['ccsExit'] == 'C$$EXIT' and not 0.95 <= float(r['ratio']) <= 1.02:
        print(f"  {r['prog']:34} {r['variant']:13} {r['ratio']}  sim={r['simCycles']} ccs={r['ccsCycles']}")
