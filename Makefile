# Built by g++ on the development box, and by clang++ on a Mac.
#
# Both work and both are checked: every translation unit compiles under
# -Wall -Wextra -Werror -pedantic with Apple clang as well as with GNU g++, and
# the compiler that comes out is the same program. What differs is what you can
# then do with it - see "make help".
#
# Serial by design, and not merely as a preference. This box has 419 MiB of RAM
# and a class-heavy C++ translation unit was measured at 178-195 MB to compile;
# -j2 asks for twice that and meets the OOM killer. Use ./build rather than
# calling make directly - it puts the whole build inside a memory cgroup, so a
# runaway compile dies by itself instead of taking the machine down. That is
# not hypothetical: an unbounded `dnf` did exactly that on 12 August.
#
# Two different -j live in this repository and they are not related. This one is
# make's, building the compiler, and it stays at 1 because a C++ translation
# unit here costs 142 MB. cxx1's own -j compiles several files at once and is
# nothing like as hungry: a whole unit peaks at 4 MB.

# The host decides the compiler unless you say otherwise: "make CXX=g++-14" and
# "make CXX=clang++" both work anywhere either exists.
# origin, not ?=: make defines CXX itself, so ?= never fires. This overrides
# make's own default while still letting "make CXX=..." win.
UNAME_S := $(shell uname -s)
ifeq ($(origin CXX),default)
  ifeq ($(UNAME_S),Darwin)
    CXX := clang++
  else
    CXX := g++
  endif
endif

# The headers cxx1 ships live in lib/, and are found by an absolute path baked in
# here because nothing installs this compiler - it runs from the tree it was
# built in. Taken from $(CURDIR) rather than written down, so a clone built
# somewhere else finds its own lib/ and not the one belonging to the tree this
# was written in.
#
# lib/ rather than include/, because none of what is in there is the language.
# The compiler is src/; the library it happens to ship is a separate thing that
# a program may ignore, replace with -I, or never reach for at all.
INCDIR   = $(CURDIR)/lib
# The C++ headers, which wrap the C ones above rather than replacing them.
CXXINCDIR = $(CURDIR)/include
# -pthread and not -lpthread: it sets the flags std::thread needs at compile
# time as well as naming the library, and getting only the library gives a
# binary that links and then misbehaves when it runs its threads.
#
# c++14, and src/ is written to it. The one thing that ever wanted C++17 was
# std::string_view, for the borrowed text an operand carries; src/backend/
# Spelling.h has a small Str of its own in its place, and nothing else in src/
# reaches past C++14. Apple's libc++ hands you string_view in C++14 mode
# anyway, so a Mac build will not catch that kind of slip - g++ on the box
# will, which is the reason to build there before believing it.
CXXFLAGS = -std=c++14 -O2 -g -Wall -Wextra -Werror -pedantic -pthread \
           -DCXX1_INCLUDE_DIR='"$(INCDIR)"' \
           -DCXX1_CXX_INCLUDE_DIR='"$(CXXINCDIR)"'
# src/backend holds one file per platform: the sizes its types measure, the ABI
# facts the front end has to know, and the code generator when there is one.
# src/parser holds the twelve files one class is split over - see its Parser.cpp.
#
# src/optimizer holds the passes over one function and the Optimizer that runs
# them in front of the real Spelling - see docs/OPTIMIZER-ARCHITECTURE.md.
#
# **A basename may not repeat across these four directories.** Objects here go
# under obj/ mirroring src/, so a collision would be harmless - but msvc/
# build.cmd gives cl a single flat /Fo directory, and there the second
# src/parser/Type.cpp would quietly overwrite the object made from src/Type.cpp.
# It is the reason the parser's files kept their ParserXxx names on moving into
# a directory that would have let them drop the prefix.
# Filtered on src/%.cpp rather than taken raw, as cc1's Makefile is. macOS
# leaves "keep both" duplicates - `Tms6747 2.cpp` beside `Tms6747.cpp`, and two
# of those appeared here mid-session - and $(wildcard) splits such a name into
# two words before anything can test it for a space, so make would try to
# build `src/backend/Tms6747` and `2.cpp` as sources. Requiring both the prefix
# and the suffix drops both halves and keeps every real source.
SRCS     = $(filter src/%.cpp,$(wildcard src/*.cpp) $(wildcard src/parser/*.cpp) \
                              $(wildcard src/backend/*.cpp) $(wildcard src/optimizer/*.cpp))
# Objects and their dependency files go under obj/ rather than beside the
# sources they came from, so that a listing of src/ is the code and nothing
# else. The tree under obj/ mirrors src/ - src/backend/X86_64.cpp becomes
# obj/backend/X86_64.o - so two files of the same name in different directories
# cannot collide, which a flat object directory would let them do.
#
# obj/ and not build/: there is already a script called build at the root.
OBJDIR   = obj
OBJS     = $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRCS))
DEPS     = $(OBJS:.o=.d)
# Where the finished program goes. `.` is this directory, which is what every
# suite, script and habit here already expects - so a plain `make` is unchanged
# by this being a parameter at all. What it buys is that the workspace build
# can name one directory and have all three programs built into it, rather than
# building them in three places and collecting them afterwards. A collection
# step is a step that can be forgotten, and was.
BINDIR  ?= .

# cxx1.exe on every machine, not only Windows. The programs in this family -
# RStudio, cc1, shc and this one - carry one name each wherever they are, and a
# suffix that changes by platform is one more thing a script has to know.
# The program's name, once - src/Name.h and the project files RIDE's
# tools/make-projects.py writes spell it the same.
PROGRAM  = cpp11
TARGET   = $(BINDIR)/$(PROGRAM).exe

.PHONY: all test golden corpus open comments clean help

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# -MMD -MP writes obj/X.d beside obj/X.o saying which headers went into it, and
# -include below reads them back. That replaces a rule that made every object
# depend on every header: correct, but it rebuilt all fifteen whenever any
# header was touched, and - worse - it was a list this file kept by hand.
#
# Getting this wrong is not a link error. A stale object compiled against an
# older class layout links perfectly well, because the mangled names still
# match, and the program then misbehaves somewhere else entirely. Compiler-S
# had exactly that: half its translation units keeping an old layout, and the
# compiler corrupting its own heap three passes away.
$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Both suites run anywhere cxx1 builds. run.sh needs to assemble and link, so
# it covers the host target only; emit.sh stops at assembly and therefore
# checks all three backends on any machine. Neither is a differential suite
# yet - comparing cxx1's objects against clang's needs mangling and
# extern "C", which are rung 2.
# The suites are told which binary, since a BINDIR build puts cpp11.exe
# somewhere other than here; unset, each falls back to ./cpp11.exe.
test: $(TARGET)
	@CXX1=$(TARGET) ./tests/run.sh
	@CXX1=$(TARGET) ./tests/emit.sh
	@CXX1=$(TARGET) ./tests/names.sh
	@CXX1=$(TARGET) ./tests/overload.sh
	-@./tools/comment-lines --count

# The comment-line policy, on its own, because it is about the source and not
# about the compiler: three lines to a group, one over a one-liner. `make test`
# reports the count and does not fail on it (the leading -); this target gates.
comments:
	@./tools/comment-lines

help:
	@echo "make            build cxx1 with $(CXX)"
	@echo "make test       build and run the four suites"
	@echo "make golden     record what emit.sh emits now, to compare a change against"
	@echo "make corpus     run the 424 inherited C cases; gates nothing"
	@echo "make open       run the register of known-open defects; gates nothing"
	@echo "make comments   check the comment-line policy and name every breach"
	@echo "make clean"
	@echo ""
	@echo "cxx1 emits assembly for x86_64-linux, x86_64-windows and"
	@echo "arm64-darwin. It builds anywhere this Makefile does; -arch picks"
	@echo "the target, and one that is not this machine implies -S."

# Before a change that is meant to emit exactly what it emits now: record, make
# the change, run the suite, and it says how many files came out different.
golden: $(TARGET)
	@CXX1=$(TARGET) ./tests/emit.sh --record

# The inherited C corpus. Not part of `make test` and not a pass rate - see
# tests/c-corpus/README, which says what each part of the failing set is.
corpus: $(TARGET)
	@CXX1=$(TARGET) ./tests/corpus.sh

# The register of known-open defects. Not part of `make test` either, and for a
# sharper reason: every program in it is a wrong answer, so it would be red by
# construction. It says how many still differ from clang - see tests/open/README.
open: $(TARGET)
	@CXX1=$(TARGET) ./tests/open.sh

clean:
	rm -rf $(OBJDIR) $(TARGET)
	rm -rf tests/out-run tests/out-emit tests/out-corpus tests/out-open
# **tests/out-emit.golden is deliberately not on that line**, and this is the
# exception the rule below is otherwise right about: a golden is recorded before
# a change and read after one, with a rebuild in between, so a clean that took it
# away would delete the baseline exactly when it was about to be used. Remove it
# by hand - rm -rf tests/out-emit.golden - or record a new one over it.
# A suite added since this rule was written leaves its output behind, and the
# list is the only place that says so. tests/out-cross survived a clean until
# 2026-08-26 for exactly that reason - eight object files nobody was looking
# for. Any new tests/out-* belongs on the line above the moment it exists.
