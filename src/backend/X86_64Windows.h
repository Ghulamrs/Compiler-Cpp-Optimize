#pragma once

#include "Backend.h"

class WindowsX86_64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULongLong; }

    Kind wcharType() const override { return Kind::UShort; }
    bool microsoftNames() const override { return true; }
    const char *threadAtexit() const override { return "__tlregdtor"; }
    const char *name() const override { return "x86_64-windows"; }
};

// **Which assembler this target is written for** arrives as a Syntax at the two calls below,
// from the Driver that read `-masm=` off argv - see Syntax in Backend.h.

class X86_64WindowsBackend final : public Backend {
public:
    const char *name() const override { return "x86_64-windows"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    bool emitsLineTable(Syntax syntax) const override;
    bool optimizesAroundVolatile() const override { return true; }
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, Syntax syntax) const override;
private:
    WindowsX86_64Target target_;
};
