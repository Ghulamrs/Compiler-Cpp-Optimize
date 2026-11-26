#pragma once

#include "../Abi.h"
#include "../Ast.h"
#include "../Type.h"

#include <iosfwd>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Source;

class CodeGen : public Visitor {
public:
    ~CodeGen() override = default;
    virtual void run(const Program &program) = 0;

    virtual void setLineSource(const Source *, const std::string &) {}
    // -O1 and -O2; a code generator with no optimizer takes 0 for either.
    virtual void setOptimize(int level) { (void)level; }
    // -Os: the smaller code where the generator has a choice; one with none takes -O1 as it stands.
    virtual void setOptimizeForSize(bool size) { (void)size; }
};

// TlsData and TlsBss hold a thread_local's template, which every thread gets a copy of.
enum class Segment { Code, Const, ConstRelocated, Data, Bss, TlsData, TlsBss };

Segment segmentFor(const Global &g);

// **A thread_local's Itanium wrapper and initialiser**, `_ZTW` and `_ZTH` before the variable's own
// encoding - what follows its `_Z`, or its name as a <source-name> where it has no `_Z`. Measured
// from clang: `_ZTW1t` for `t`, `_ZTWN2ns1nE` for `ns::n`, `_ZTWL1s` for a `static` one.
std::string tlsWrapperName(const std::string &symbol);
std::string tlsInitName(const std::string &symbol);

// **Which assembler x86_64-windows is written for**, the one target with a choice: GNU syntax for clang; MASM for the project's own assembler, which reads `SEGMENT ... COMDAT`; or MASM without COMDAT for ml64, which cannot, and so links one translation unit at a time. The other targets ignore it.
enum class Syntax { Gnu, Masm, Ml64 };

class Backend {
public:
    virtual ~Backend() = default;

    virtual const char *name() const = 0;
    virtual const Target &target() const = 0;
    virtual const Abi &abi() const = 0;

    // **The syntax is passed, not looked up.**
    virtual std::unique_ptr<CodeGen> codegen(std::ostream &sink,
                                             Syntax syntax) const = 0;
    virtual bool emits() const = 0;

    virtual bool emitsLineTable(Syntax syntax) const { (void) syntax; return false; }
    // Whether a function that touches a volatile lvalue is written past the optimizer one by one, so a
    // file that writes `volatile` keeps its -O; where not, the driver compiles that file without.
    virtual bool optimizesAroundVolatile() const { return false; }

    virtual const char *const *identityMacros() const = 0;
    // [intro.compliance]/7: a freestanding implementation is one with no operating system to run on; it
    // provides [compliance]'s headers and __STDC_HOSTED__ is 0.
    virtual bool hosted() const { return true; }
};

std::vector<std::pair<std::string, std::string> > predefinedMacros(const Backend &b);

// A name past ASCII quoted for GNU as, `"_caf\u00e9"@PAGE` in UTF-8; tms6747's refusal, or empty.
std::string spellWideNames(const Backend &b, std::string *text);

const Backend *findBackend(const std::string &name);
const Backend &defaultBackend();
std::string backendNames();
