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
};

enum class Segment { Code, Const, ConstRelocated, Data, Bss };

Segment segmentFor(const Global &g);

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

    virtual const char *const *identityMacros() const = 0;
};

std::vector<std::pair<std::string, std::string> > predefinedMacros(const Backend &b);

const Backend *findBackend(const std::string &name);
const Backend &defaultBackend();
std::string backendNames();
