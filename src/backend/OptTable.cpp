#include "OptTable.h"

#include <cstring>
#include <map>

namespace opt {

namespace {

using K = Opcode;
constexpr unsigned E = K::kExplicit, W = K::kWritesOnly, I = K::kImmSource, R = K::kRenamable;
constexpr unsigned S = K::kShift, Z = K::kZeroIdiom, N = K::kNoFlags, M = K::kMergeXmm;

// **The table.** One line per mnemonic; the flags and widths are those the
// passes recognised by name before, so that nothing they emit changes.
const Opcode kTable[] = {
    {"call", K::Call, 0, 0},
    {"ret", K::Ret, 0, 0},
    {"jmp", K::Jmp, 0, 0},
    {"push", K::Push, R, 0},
    {"pushq", K::Push, R, 0},
    {"pop", K::Pop, 0, 0},
    {"popq", K::Pop, 0, 0},
    {"leave", K::Leave, 0, 0},
    {"cqo", K::SignExtendAx, 0, 0},
    {"cqto", K::SignExtendAx, 0, 0},
    {"cdq", K::SignExtendAx, 0, 0},
    {"cltd", K::SignExtendAx, 0, 0},
    {"cltq", K::ExtendAx, 0, 0},
    {"cdqe", K::ExtendAx, 0, 0},
    {"rep movsq", K::StringMove, 0, 0},
    {"idiv", K::Div, 0, 0},
    {"div", K::Div, 0, 0},
    {"idivl", K::Div, 0, 0},
    {"divl", K::Div, 0, 0},
    {"idivq", K::Div, 0, 0},
    {"divq", K::Div, 0, 0},
    // Compares: the flags are their only result.
    {"cmp", K::Compare, E | I | R, 0},
    {"cmpl", K::Compare, E | I | R, 4},
    {"cmpq", K::Compare, 0, 8},
    {"cmpb", K::Compare, 0, 1},
    {"test", K::Compare, E | R, 0},
    {"testb", K::Compare, 0, 0},
    {"testl", K::Compare, 0, 0},
    {"testq", K::Compare, 0, 0},
    {"ucomisd", K::Compare, 0, 0},
    {"ucomiss", K::Compare, 0, 0},
    {"comisd", K::Compare, 0, 0},
    {"comiss", K::Compare, 0, 0},
    // One operand, read and written.
    {"neg", K::Unary, 0, 0},
    {"negq", K::Unary, 0, 0},
    {"inc", K::Unary, 0, 0},
    {"dec", K::Unary, 0, 0},
    {"not", K::Unary, N, 0},
    {"notq", K::Unary, N, 0},
    // Moves: the destination is written whole, or in part where the width says.
    {"mov", K::Move, E | W | I | R, 0},
    {"movq", K::Move, E | W | I | R, 8},
    {"movl", K::Move, E | W | I | R, 4},
    {"movw", K::Move, E | W | I, 2},
    {"movb", K::Move, E | W | I, 1},
    {"movabs", K::Move, E | W, 0},
    {"movslq", K::Move, E | W | R, 0},
    {"movsbq", K::Move, E | W, 0},
    {"movswq", K::Move, E | W, 0},
    {"movsbl", K::Move, E | W, 0},
    {"movswl", K::Move, E | W, 0},
    {"movzbq", K::Move, E | W, 0},
    {"movzbl", K::Move, E | W, 0},
    {"movzwl", K::Move, E | W, 0},
    {"movzwq", K::Move, E | W, 0},
    {"lea", K::Move, E | W | K::kAddress, 0},
    {"movd", K::Move, E | W, 0},
    {"movaps", K::Move, E | W, 0},
    {"movapd", K::Move, E | W, 0},
    {"movsd", K::Move, E | W | M, 0},
    {"movss", K::Move, E | W | M, 0},
    // Two-operand arithmetic: b is read and written.
    {"add", K::Rmw, E | I | R, 0},
    {"sub", K::Rmw, E | I | R | Z, 0},
    {"and", K::Rmw, E | I | R, 0},
    {"or", K::Rmw, E | I | R, 0},
    {"xor", K::Rmw, E | I | R | Z, 0},
    {"adc", K::Rmw, 0, 0},
    {"sbb", K::Rmw, 0, 0},
    {"imul", K::Rmw, E | I | R, 0},
    {"shl", K::Rmw, S, 0},
    {"sar", K::Rmw, S, 0},
    {"shr", K::Rmw, S, 0},
    {"addl", K::Rmw, E | I | R, 4},
    {"subl", K::Rmw, E | I | R, 4},
    {"orb", K::Rmw, 0, 1},
    {"andl", K::Rmw, 0, 0},
    {"orl", K::Rmw, 0, 0},
    {"xorl", K::Rmw, Z, 0},
    {"shll", K::Rmw, S, 0},
    {"sarl", K::Rmw, S, 0},
    {"shrl", K::Rmw, S, 0},
    {"addsd", K::Rmw, N, 0},
    {"subsd", K::Rmw, N, 0},
    {"mulsd", K::Rmw, N, 0},
    {"divsd", K::Rmw, N, 0},
    {"addss", K::Rmw, N, 0},
    {"subss", K::Rmw, N, 0},
    {"mulss", K::Rmw, N, 0},
    {"divss", K::Rmw, N, 0},
    {"pxor", K::Rmw, N | Z, 0},
    {"xorps", K::Rmw, N | Z, 0},
    {"xorpd", K::Rmw, N | Z, 0},
    {"andpd", K::Rmw, N, 0},
    {"andps", K::Rmw, N, 0},
    // Known by some pass, but opaque to the effects: a shift by name only, a
    // suffix that names a width on an instruction no pass rewrites.
    {"sal", K::Unknown, S, 0},
    {"rol", K::Unknown, S, 0},
    {"ror", K::Unknown, S, 0},
    {"addq", K::Unknown, 0, 8},
    {"subq", K::Unknown, 0, 8},
    // x87 compares set the flags; the rest of the x87 leaves them alone.
    {"fucomip", K::X87, 0, 0},
    {"fcomip", K::X87, 0, 0},
    {"fucomi", K::X87, 0, 0},
    {"fcomi", K::X87, 0, 0},
};

// The families a prefix names, for a mnemonic not listed above.
const Opcode kMovFamily = {"mov*", K::Unknown, E | W, 0};
const Opcode kSetFamily = {"set*", K::Unknown, E | W, 0};
const Opcode kSetcc = {"setcc", K::Setcc, E | W, 0};
const Opcode kJcc = {"jcc", K::Jcc, 0, 0};
const Opcode kCvtFamily = {"cvt*", K::Move, M | K::kCvt, 0};
const Opcode kX87Family = {"f*", K::X87, N, 0};
const Opcode kUnknown = {"?", K::Unknown, 0, 0};

bool starts(const std::string &m, const char *p) { return m.compare(0, std::strlen(p), p) == 0; }

// The conditions jcc, setcc and cmovcc spell, each with its opposite.
struct Cond { const char *cc, *inv; };
const Cond kConds[] = {
    {"e", "ne"}, {"ne", "e"}, {"z", "nz"}, {"nz", "z"}, {"l", "ge"}, {"ge", "l"},
    {"le", "g"}, {"g", "le"}, {"b", "ae"}, {"ae", "b"}, {"be", "a"}, {"a", "be"},
    {"s", "ns"}, {"ns", "s"}, {"p", "np"}, {"np", "p"}, {"o", "no"}, {"no", "o"},
};

}

const Opcode &opcodeOf(const std::string &m) {
    static std::map<std::string, const Opcode *> index;
    if (index.empty())
        for (const Opcode &o : kTable) index[o.name] = &o;
    const auto it = index.find(m);
    if (it != index.end()) return *it->second;
    if (!conditionOf(m).empty()) return m[0] == 'j' ? kJcc : kSetcc;
    if (starts(m, "mov")) return kMovFamily;
    if (starts(m, "set")) return kSetFamily;
    if (starts(m, "cvt")) return kCvtFamily;
    if (m[0] == 'f') return kX87Family;
    return kUnknown;
}

bool isMovQ(const std::string &m) { return m == "mov" || m == "movq"; }
bool isMovQL(const std::string &m) { return isMovQ(m) || m == "movl"; }
bool isMovAny(const std::string &m) { return isMovQL(m) || m == "movw" || m == "movb"; }
bool isPush(const std::string &m) { return m == "push" || m == "pushq"; }
bool isPop(const std::string &m) { return m == "pop" || m == "popq"; }

std::string conditionOf(const std::string &m) {
    std::string rest;
    if (m.size() > 1 && m[0] == 'j' && m != "jmp") rest = m.substr(1);
    else if (starts(m, "set")) rest = m.substr(3);
    else return std::string();
    for (const Cond &c : kConds) if (rest == c.cc) return rest;
    return std::string();
}

std::string inverse(const std::string &cc) {
    for (const Cond &c : kConds) if (cc == c.cc) return c.inv;
    return std::string();
}

}
