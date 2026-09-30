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

## Measured: adventure.cpp, cycle.Total on the CCS 5.5 C6747 cycle-accurate simulator, 2026-09-30

Tree `8682cf0` (tms-opt), every run a session of its own, every fingerprint 1732262721 -
clang's. "input" is the harness reading the file from the host; "embedded" the string
harness, `harnessFile=0`; "rounds 0" the input harness reading the file and compiling
nothing, so it is start-up plus the read plus the printing.

| build | input, rounds 1 | input, rounds 0 | embedded | input - embedded | input - rounds 0 - embedded |
| --- | --- | --- | --- | --- | --- |
| cpp11 -O2, ASM6x, lnk6x 7.4.4 | 508,981,222 | 16,715,927 | 485,167,021 | +23,814,201 | +7,098,274 |
| cpp11 -O2, ASM6x, LNK6x | 508,981,222 | 16,715,927 | 485,167,021 | +23,814,201 | +7,098,274 |
| cl6x 7.4.4 -O2 (CCS 5.5) | 513,524,185 | 5,155,672 | 504,043,612 | +9,480,573 | +4,324,901 |
| cl6x 8.2.2 -O2 (CCS 7.4) | 560,320,894 | 7,901,348 | 518,513,980 | +41,806,914 | +33,905,566 |

cpp11 against cl6x 7.4.4: 0.991 reading from the host, 0.963 embedded; against 8.2.2:
0.908 and 0.936. The two linkers give the same count to the cycle on both harnesses.

**Where the reads fall: inside the timed region, and they are not the whole of the
difference.** The clock runs from load to `C$$EXIT`, so the read is timed with the compile.
Its cost differs by library - cpp11's `<istream>` takes the file a byte at a time through
`fgetc`, 16.7 M cycles for 3,849 bytes with start-up and the two printed lines; Dinkumware's
`filebuf` 5.2 M - and what is left after subtracting the rounds-0 run is still 4 M to 34 M
of compile that the embedded harness does not pay. The file arrives as a `std::string`
grown a byte at a time and then copied, which leaves the heap in a different state than a
literal in `.const` does; the compile's own allocations then meet a longer free list in
TI's `free`, which the profile already showed to be where this program's time goes. That
is the program's real behaviour and not an artefact - a compiler that reads a file has a
heap that has read a file - but it is not a cost of CIO, and it is not the same on the
three builds.

**What CIO itself costs is small and is the TI runtime's**: the same `fread`/`fgetc` code
in every build, ~115 cycles a byte through `fread` and ~630 through `fgetc` on the probe.
The host's service time is not simulated cycles - wall time only - so the count is not
inflated by the host, and there is no way to say "that is what a DSP's own I/O costs"
either; on hardware the bytes would come from flash, a UART or a file system, at a cost
that has nothing to do with these numbers.

**Recommendation.** Make the input-reading build the primary number and report it beside
its rounds-0 run, per file: what the program is measured doing is then what a compiler
does - opens the file it was named, reads it, compiles it - and the reader can see how
much of the count is the read. Keep the embedded harness as the control that isolates the
compile from the heap the read leaves behind; the two disagree by 2% to 8% depending on
the build, and that disagreement is itself a finding about `free`. Do not subtract the
rounds-0 number and call the remainder "the compile": the table above shows it is not.
If the read is ever to be taken out of the timed region for real, the only way is on the
program's side - read every file into memory, then reset the count - and this simulator
offers no counter reset from inside the program and no breakpoint DSS can stop it at
(runca.js records that); a `printf` marker in the CIO log gives an order of events and no
cycle stamp. So it stays inside, measured, and stated.
