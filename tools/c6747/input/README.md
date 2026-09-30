# Compiler++ reading its input from the host, on the C6747 simulator

The timed harness (`tools/c6747/compilerpp/make-harness.py --workload ... --rounds n`)
compiles the ten workload files in as string literals, because a program on the C6747
simulator was assumed to have no files and no argv. Measured 2026-09-30 on the Windows
box's CCS 5.5 C6747 cycle-accurate simulator: it has both.

- **argv**: link with `--args=N` (lnk6x 7.4.4, lnk6x 8.2.2, and LNK6x from `d972293`),
  and DSS's `session.memory.loadProgram(file, [args])` writes argc and the strings into
  the `.args` section. The array is *the whole of argv* - DSS puts no program name in
  front, so the first element passed is `argv[0]`. `tools/c6747/runargs.js` is
  `runca.js` plus that: everything after a `--` argument goes to the program.
- **files**: `fopen`, `fread`, `fgetc` in the TI runtime go to the host through CIO
  (the `C$$IO$$` breakpoint DSS services). A path is the host's - `C:/cxx1/...`,
  forward slashes accepted - and `"r"` on the host gives the file's bytes unchanged
  (a 3849-byte file read by `fread` and by `fgetc` both matched the host's FNV-1a).

`argv-file-probe.cpp` is the probe, compiled by cl6x 7.4.4 and by cpp11+ASM6x, the
latter linked by lnk6x 7.4.4 and by LNK6x `--args=1024`: all three print their argv and
both checksums. Cycles (cycle.Total) for the cpp11 build: 3,170,179 (7.4.4 link) and
3,170,209 (LNK6x). What the reads cost, cl6x 7.4.4's build of the same probe, no file
78,271; `fread` whole, 3849 bytes: 517,926; `fgetc` per byte: 2,518,984; both: 2,927,268.
So a byte through `fgetc` is about 630 cycles and through `fread` about 115 - the
TI runtime's own stdio, the same code in every build.

## The input-reading harness

`make-harness.py ... --input` writes the same harness with no source compiled in: `main`
takes the file's path from `argv[1]` and reads it exactly as Compiler++'s own `main.cpp`
does - `std::ifstream` and `ss << in.rdbuf()` - so under cpp11 that is `fgetc` per byte
through cpp11's `<istream>`, and under cl6x it is Dinkumware's `filebuf`. `argv[2]` is the
number of rounds; **0 reads the file and compiles nothing**, which is how the read is
measured apart from the compile: the simulator's clock runs from load to `C$$EXIT` and
nothing inside the program can start or stop it. The line printed is the one the
embedded harness prints, so the fingerprint check against clang's native build is the
same comparison.

## The scripts, for `C:\cxx1\input\cpp` on the box

    build-input.cmd     hin\ (input) and hemb\ (embedded) each by cl6x 7.4.4 -O2, cl6x 8.2.2
                        -O2, and cpp11's shipped object linked by lnk6x 7.4.4 - the input ones
                        with --args=1024; LNK6x's images are linked on the Mac and shipped
    run-one-input.cmd   <image> <tag> <NAME=VALUE|-> [args...]: one simulator run, its own
                        Eclipse workspace, 30-minute cap, waiting up to 30 minutes for an
                        image still being built
    lane.cmd <list>     the runs of a list file, one after another
    hold.cmd            starts laneA.txt and laneB.txt - two runs at once and no more - and
                        stays until both are done, so the ssh session that started it lives
                        that long (start /b children die with the session)

What the Mac ships: `hin/` and `hemb/` each holding `harness.cpp`,
`compilerpp_amalgamated.cpp`, `h.cpp11.obj` and `lnk6x.out`, the `workload/`, `runargs.js`,
the ccxml, `C6747-ddr.cmd`, and the probe's object and LNK6x image.
