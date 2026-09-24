#include "OptEffects.h"

#include "../Abi.h"

namespace opt {

namespace {

// What an operand reads when it is only a source: its register, or the
// register an address is formed from.
RegSet readsOf(const Operand &o) {
    switch (o.kind) {
    case Operand::Register:
    case Operand::Indirect:
    case Operand::Memory: return o.reg.id >= 0 && o.reg.id < kPhysical ? bit(o.reg.id) : 0;
    default: return 0;
    }
}

bool inMemory(const Operand &o) { return o.kind == Operand::Memory || o.kind == Operand::RipSymbol; }

// **A write of four or eight bytes is whole**; a narrower one leaves the rest.
void write(Effects &e, const Operand &o) {
    if (inMemory(o)) { e.memoryWritten = true; e.reads |= readsOf(o); return; }
    if (o.kind != Operand::Register) return;
    if (o.reg.id < 0) { e.opaque = true; return; }
    if (o.reg.id >= kPhysical) return;                  // a pseudo: the IR's to track
    if (o.reg.id < kGprs && o.reg.width < 4) e.partial |= bit(o.reg.id);
    else e.writes |= bit(o.reg.id);
}

void read(Effects &e, const Operand &o) {
    e.reads |= readsOf(o);
    if (inMemory(o)) e.memoryRead = true;
    if (o.kind == Operand::Register && o.reg.id < 0) e.opaque = true;
}

}

bool explicitOnly(const Instr &i) {
    if (i.m == "imul" && i.operands != 2) return false;           // rdx:rax, unnamed
    return opcodeOf(i.m).has(Opcode::kExplicit);
}

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

// **A register named at four bytes or fewer is read only there**; anything
// implicit, or an address, is read whole.
RegSet wideOf(const Instr &i, const Effects &e) {
    if (!explicitOnly(i)) return e.reads;
    // A move's destination, and setcc's, is written, not read.
    const bool writesOnly = opcodeOf(i.m).has(Opcode::kWritesOnly);
    RegSet narrow = 0, wide = 0;
    for (const Operand *o : {&i.a, &i.b}) {
        const bool destination = o == (i.operands == 1 ? &i.a : &i.b) && writesOnly;
        if (gpr(*o) && !destination) (o->reg.width <= 4 ? narrow : wide) |= bit(o->reg.id);
        else if (o->kind == Operand::Memory && o->reg.id >= 0) wide |= bit(o->reg.id);
    }
    return e.reads & ~(narrow & ~wide);
}

}

Convention conventionOf(const Abi &abi) {
    Convention c;
    for (int i = 0; i < abi.intCount; ++i) c.arguments |= bit(parseReg(abi.intRegs[i]).id);
    for (int i = 0; i < abi.sseCount; ++i) c.arguments |= bit(parseReg(abi.sseRegs[i]).id);
    if (abi.variadicSseCountInAl) c.arguments |= bit(RAX);
    for (int i = 0; i < abi.preservedCount; ++i) c.preserved |= bit(parseReg(abi.preservedRegs[i]).id);
    c.clobbered = kAllRegs & ~c.preserved;
    c.returned = bit(RAX) | bit(RDX) | bit(kXmm0) | bit(kXmm0 + 1);
    return c;
}

namespace {

// **What an instruction does with each operand it names, and what it does
// beyond them** - one branch per kind in the opcode table, read by both
// effectsOf and rolesOf.
Roles classify(const Instr &i, const Convention &conv, Effects &e) {
    const Opcode &op = opcodeOf(i.m);
    Roles r;
    switch (op.kind) {
    case Opcode::Call:
        r.a = kRead;
        e.control = e.memoryRead = e.memoryWritten = true;
        e.reads = conv.arguments | bit(RSP);
        e.writes = conv.clobbered & ~bit(RSP);
        e.flagsWritten = true;
        break;
    case Opcode::Ret:
        e.control = true;
        e.reads = conv.returned | conv.preserved | bit(RSP);
        break;
    case Opcode::Jmp:
        e.control = true;
        if (i.a.kind != Operand::Label) { r.a = kRead; e.opaque = true; }
        break;
    case Opcode::Jcc:
        e.control = true;
        e.flagsRead = true;
        break;
    case Opcode::Setcc:
        r.a = kWrite;
        e.flagsRead = true;
        break;
    case Opcode::Push:
        r.a = kRead;
        e.reads = e.writes = bit(RSP);
        e.memoryWritten = e.stack = true;
        break;
    case Opcode::Pop:
        r.a = kWrite;
        e.reads = e.writes = bit(RSP);
        e.memoryRead = e.stack = true;
        break;
    case Opcode::Leave:
        e.reads = bit(RBP);
        e.writes = bit(RSP) | bit(RBP);
        e.memoryRead = e.stack = true;
        break;
    case Opcode::SignExtendAx:
        e.reads = bit(RAX);
        e.writes = bit(RDX);
        break;
    case Opcode::StringMove:
        // rcx words from (rsi) to (rdi): all three read, and left changed.
        e.reads = e.writes = bit(RSI) | bit(RDI) | bit(RCX);
        e.memoryRead = e.memoryWritten = true;
        break;
    case Opcode::ExtendAx:
        e.reads = e.writes = bit(RAX);
        break;
    case Opcode::Div:
        r.a = kRead;
        e.reads = e.writes = bit(RAX) | bit(RDX);
        e.flagsWritten = true;
        break;
    case Opcode::Compare:
        r.a = r.b = kRead;
        e.flagsWritten = true;
        break;
    case Opcode::Unary:
        r.a = kRead | kWrite;
        e.flagsWritten = !op.has(Opcode::kNoFlags);
        break;
    case Opcode::Move: {
        r.a = op.has(Opcode::kAddress) ? kAddress : kRead;
        const bool keeps = op.has(Opcode::kMergeXmm) && i.b.kind == Operand::Register && i.a.kind == Operand::Register;
        r.b = keeps ? kWrite | kKeep : kWrite;
        break;
    }
    case Opcode::Rmw:
        if (i.operands != 2) { e.opaque = true; break; }   // imul's rdx:rax, a shift by one
        {
            const bool zeroing = op.has(Opcode::kZeroIdiom) &&
                                 i.a.kind == Operand::Register && i.b.kind == Operand::Register &&
                                 i.a.reg.id == i.b.reg.id && i.a.reg.id >= 0;
            r.a = zeroing ? 0 : kRead;
            r.b = zeroing ? kWrite : kRead | kWrite;
            e.flagsWritten = !op.has(Opcode::kNoFlags);
            if (i.b.isReg(RSP)) e.stack = true;
        }
        break;
    case Opcode::X87:
        r.a = kRead;
        e.memoryRead = e.memoryWritten = true;
        e.flagsWritten = !op.has(Opcode::kNoFlags);
        break;
    case Opcode::Unknown:
        e.opaque = true;
        break;
    }
    return r;
}

void apply(Effects &e, const Operand &o, unsigned role) {
    if (role & kAddress) { e.reads |= readsOf(o); return; }
    if (role & kRead) read(e, o);
    if (!(role & kWrite)) return;
    if ((role & kKeep) && o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kPhysical)
        e.partial |= bit(o.reg.id);
    else write(e, o);
}

}

Roles rolesOf(const Instr &i) {
    Effects ignored;
    return classify(i, Convention(), ignored);
}

Effects effectsOf(const Instr &i, const Convention &conv) {
    Effects e;
    const Roles r = classify(i, conv, e);
    apply(e, i.a, r.a);
    apply(e, i.b, r.b);

    if (e.opaque) {
        e.reads = e.writes = kAllRegs;
        e.flagsRead = e.flagsWritten = e.memoryRead = e.memoryWritten = true;
    }
    e.reads |= e.partial;
    e.wide = wideOf(i, e);
    return e;
}

}
