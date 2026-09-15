# A Makefile for a project compiled by cxx1.
#
# ©2026 G. R. Akhtar - ISO C++ 11,  Compiler
#
# Copy this beside your own sources and change the first three lines. It builds
# every .cpp in this directory into one program, through an object file each, so
# that editing one source recompiles one source.
#
#   make            build $(TARGET)
#   make run        build it and run it
#   make one        the same program built in a single cxx1 command
#   make asm        stop at assembly, one .s per source, for reading
#   make clean

# ---- what to build ----------------------------------------------------------

TARGET  := demo
SRCS    := $(wildcard *.cpp)
OBJS    := $(SRCS:.cpp=.o)
ASMS    := $(SRCS:.cpp=.s)

# ---- the compiler -----------------------------------------------------------
#
# **CXX1 is the whole toolchain as far as this file is concerned.** It runs the
# platform's assembler and linker for you - see "which tools take part" in
# README.1ST - so there is no AS or LD here, and no library path either: the
# headers ship with the compiler and are found beside it.
#
# Point CXX1 at wherever the compiler was unpacked, or leave it as a bare name
# if it is on PATH.

CXX1    ?= cxx1.exe
CXX1FLAGS ?=
# Examples of what may go on that line, none of them needed by default:
#   -I include        a directory of your own headers, searched first
#   -D NDEBUG=1       define a macro
#   -g                a line table, so a debugger can step through the source
#   -j 4              how many sources compile at once, when there are several
#   -nologo           leave out the line the compiler prints before it starts

# ---- rules ------------------------------------------------------------------

.PHONY: all run one asm clean

all: $(TARGET)

# The link step. cxx1 is given the objects and hands them to the platform
# linker - `c++` on Linux and macOS, `link.exe` on Windows - which is the same
# program that would link a clang or MSVC build.
$(TARGET): $(OBJS)
	$(CXX1) $(CXX1FLAGS) $(OBJS) -o $@

# One object per source. `-c` stops after the assembler, so this is the step
# that recompiles when one file changes.
%.o: %.cpp
	$(CXX1) $(CXX1FLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

# **The same program in one command**, which is what a small project wants:
# cxx1 compiles the sources on a pool of threads and reports which thread took
# which file, then assembles and links them together.
one:
	$(CXX1) $(CXX1FLAGS) $(SRCS) -o $(TARGET)

# Assembly to read rather than to run. Add `-arch x86_64-linux` (or
# x86_64-windows, or arm64-darwin) to see what another machine would get; a
# target that is not this one stops here of its own accord, there being no
# assembler for it on this machine.
asm: $(ASMS)

%.s: %.cpp
	$(CXX1) $(CXX1FLAGS) -S $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) $(ASMS)
