#pragma once

// **CodeView, the debug information Microsoft's tools read**, written as assembly
// text for clang's COFF assembler on x86_64-windows (-masm=gnu -g). link.exe /DEBUG
// makes a PDB of it, and cdb, WinDbg and RIDE's Debug tab read that.

// It is Dwarf.cpp's counterpart and is fed the same DwarfFunction, DwarfGlobal,
// Local and Type information. Two sections: `.debug$S` holds the symbols and the
// line tables, `.debug$T` the types from index 0x1000.

// .debug$S: S_OBJNAME and S_COMPILE3; per function S_GPROC32 or S_LPROC32,
// S_FRAMEPROC, S_REGREL32 off RBP per parameter and local, S_BLOCK32 per block,
// S_END, then `.cv_linetable`; S_GDATA32 or S_LDATA32 per global.

// The line entries are the spelling's: CoffSpelling writes `.cv_file`, `.cv_func_id`
// and `.cv_loc` where GnuSpelling writes `.file` and `.loc`. A mergeable function's
// records go in a `.debug$S` associative with its COMDAT, as clang writes them.

// Measured against cl /Zi in cdb (M10's `m10.py oracle`); the design and the
// hand-written probe it grew from are RIDE's docs/M10-ANALYSIS.md and docs/m10-probe.

#include "Dwarf.h"

#include <functional>
#include <string>
#include <vector>

// S_COMPILE3's language: CV_CFL_C is 0, CV_CFL_CXX is 1.
enum class CodeViewLanguage { C = 0, Cpp = 1 };

// How a symbol or label is written where a record names it - CoffSpelling::labelText.
typedef std::function<std::string(const std::string &)> Spell;

void writeCodeView(std::string &out, const Target &target, CodeViewLanguage language,
                   const std::string &objectName,
                   const std::vector<DwarfFunction> &fns,
                   const std::vector<DwarfGlobal> &globals, const Spell &spell);

// cl's S_GPROC32 name - `geo::Shape::area`, `Box<int>::twice` - from the symbol's scopes or `this`.
std::string codeViewName(const std::string &symbol, const std::string &name, const Type *thisClass);

// A source path as `.cv_file` wants it: absolute (cdb matches a breakpoint by it), backslashes doubled.
std::string codeViewPath(const std::string &compDir, const std::string &name);
