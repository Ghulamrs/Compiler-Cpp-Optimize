#include "OptPasses.h"

#include <map>

namespace opt {

namespace {

// **What a register holds, as far as this block can tell.** An unknown value
// has a number, so two registers holding the same unknown are known equal; a
// frame address is rbp plus an offset; a condition is a setcc's 0 or 1.
struct Value {
    enum Kind { Unknown, Const, FrameAddr, Condition };
    Kind kind = Unknown;
    long long k = 0;          // the constant, or the frame offset
    int id = 0;               // an unknown's number; 0 is equal to nothing
    bool sext32 = false;      // equal to the sign extension of its low half
    std::string cc;           // Condition: what was tested, and the entry
    int flagsFrom = -1;       // whose flags it was tested on

    static Value constant(long long v) {
        Value x;
        x.kind = Const;
        x.k = v;
        x.sext32 = v == static_cast<int>(v);
        return x;
    }
    static Value frame(long long off) { Value x; x.kind = FrameAddr; x.k = off; return x; }

    bool same(const Value &o) const {
        if (kind != o.kind) return false;
        switch (kind) {
        case Const: case FrameAddr: return k == o.k;
        case Condition: return cc == o.cc && flagsFrom == o.flagsFrom;
        case Unknown: return id != 0 && id == o.id;
        }
        return false;
    }
};

bool fitsImm32(long long v) { return v == static_cast<int>(v); }
long long sext32(long long v) { return static_cast<int>(static_cast<unsigned>(v)); }
bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool is(const std::string &m, std::initializer_list<const char *> names) {
    for (const char *n : names) if (m == n) return true;
    return false;
}

// The width an instruction's own suffix names, or 0 where only a register can.
int suffixWidth(const std::string &m) { return opcodeOf(m).width; }

// The instructions whose source may be an immediate in place of a register.
bool takesImmediate(const std::string &m) { return opcodeOf(m).has(Opcode::kImmSource); }

// A constant cut to the width it is read at, as that width's instruction reads it.
long long atWidth(long long v, int width) {
    switch (width) {
    case 4: return sext32(v);
    case 2: return static_cast<short>(static_cast<unsigned short>(v));
    case 1: return static_cast<signed char>(static_cast<unsigned char>(v));
    }
    return v;
}

struct Slot { int width; Value v; };

// **One walk forward through each block**, rewriting each instruction from
// what is known before it and then learning from it. Nothing is known at a
// label, so no fact crosses an edge.
class Forward {
public:
    Forward(Stream &s, Flow &f, const Convention &c, long long tempFrom) : s_(s), f_(f), c_(c), tempFrom_(tempFrom) {}

    bool run() {
        // **A caller-saved register this function neither names nor uses
        // unnamed** (idiv's rdx, a string move's rsi) can carry a pushed value
        // to its pop. A call's unnamed reads are arguments, set by name.
        RegSet named = 0;
        for (std::size_t k = 0; k < s_.size(); ++k) {
            const Entry &e = s_[k];
            if (e.kind != Entry::Ins) continue;
            RegSet here = 0;
            for (const Operand *o : {&e.ins.a, &e.ins.b})
                if (o->kind != Operand::Immediate && o->kind != Operand::Label && o->reg.id >= 0 &&
                    o->reg.id < kPhysical)
                    here |= bit(o->reg.id);
            const Effects &fx = f_.effects[k];
            named |= here;
            if (!fx.control) named |= (fx.reads | fx.writes | fx.partial) & ~here;
        }
        for (int r = 0; r < kGprs; ++r)
            if ((c_.clobbered & bit(r)) && !frameReg(r) && !(named & bit(r))) scratch_.push_back(r);
        for (int r = kXmm0; r < kXmm0 + 16; ++r)
            if ((c_.clobbered & bit(r)) && !(named & bit(r))) xmmScratch_.push_back(r);
        // The two narrow rewrites ask what is live out of a block; solve it.
        f_.live(s_);
        flagsDead_.assign(s_.size(), false);
        for (const Block &b : f_.blocks) {
            Live live = b.out;
            for (int k = b.end - 1; k >= b.begin; --k) {
                if (s_[k].kind != Entry::Ins || s_[k].dead) continue;
                f_.joinPads(static_cast<int>(&b - &f_.blocks[0]), k, live);
                flagsDead_[k] = !live.flags;
                live.step(f_.effects[k]);
            }
        }
        for (const Block &b : f_.blocks) {
            enterBlock();
            blk_ = &b;
            for (int k = b.begin; k < b.end; ++k)
                if (s_[k].kind == Entry::Ins && !s_[k].dead) step(k);
        }
        return changed_;
    }

private:
    struct Pushed { Value v; int at; int xmm = -1; };   // xmm: the register a movsd stored, or -1
    enum class Pop { Kept, Copy, Gone };

    Stream &s_;
    Flow &f_;
    const Convention &c_;
    const long long tempFrom_;
    Value regs_[kGprs];
    std::vector<Pushed> stack_;
    std::map<long long, Pushed> temps_;  // a temporary's slot -> the store it holds, in this block
    std::map<long long, Slot> slots_;   // rbp offset -> what the frame holds there
    int flagsFrom_ = -1;
    std::vector<bool> flagsDead_;       // per entry: no path reads the flags it leaves
    const Block *blk_ = nullptr;        // the block being walked
    std::map<int, Value> sextOf_;       // an unknown's number -> its sign extension's value
    std::map<int, Value> sextFrom_;     // and the extension's number -> the unknown it extends
    int k_ = 0;                         // the entry being stepped
    int condReg_ = -1;                  // the register whose low byte a setcc just wrote
    Value cond_;
    // **The copy each register last received**, good while neither end has
    // been written since - which the versions, bumped on every write, tell.
    struct Copy { int from = -1; int width = 0; unsigned fromVersion = 0, toVersion = 0; };
    Copy copies_[kGprs];
    unsigned version_[kGprs] = {};
    int nextId_ = 1;
    bool changed_ = false;
    std::vector<int> scratch_;
    std::vector<int> xmmScratch_;

    Value fresh() { Value v; v.id = nextId_++; return v; }

    void enterBlock() {
        for (Value &v : regs_) v = fresh();
        for (Copy &c : copies_) c = Copy();
        stack_.clear();
        slots_.clear();
        temps_.clear();
        sextOf_.clear();
        sextFrom_.clear();
        flagsFrom_ = condReg_ = -1;
    }
    bool isTemp(const Operand &o) const { return tempFrom_ != 0 && o.isMem() && o.reg.id == RBP && o.scale == 0 && o.disp <= tempFrom_; }

    // In this order: an address folded first may be a slot the reload finds,
    // and a temporary's load is paired before the general reload resolves it
    // and leaves its store standing.
    void step(int k) {
        k_ = k;
        Instr i = s_[k].ins;
        bool edited = foldAddress(i.a);
        edited = foldAddress(i.b) || edited;
        edited = immediateSource(i) || edited;
        Pop pop = pairTemp(i, k);
        if (pop == Pop::Kept) edited = reloadFromRegister(i) || edited;
        edited = readOriginal(i) || edited;
        edited = addressOfSum(i, k) || edited;
        edited = extensionHeld(i) || edited;
        edited = extensionUnread(i, k) || edited;
        if (pop == Pop::Kept) pop = pairPop(i, k);
        if (pop == Pop::Gone || isNoop(i) || foldCondition(i, k)) { kill(k); return; }
        if (edited || pop == Pop::Copy) replace(k, i);
        learn(s_[k].ins, k);
    }

    void replace(int k, const Instr &i) {
        s_[k].ins = i;
        f_.effects[k] = effectsOf(i, c_);
        changed_ = true;
    }
    void kill(int k) { s_[k].dead = true; changed_ = true; }

    // Which register other than `avoid` holds v now; -1 if none.
    int holding(const Value &v, int avoid) const {
        for (int r = 0; r < kGprs; ++r)
            if (r != avoid && r != RSP && r != RBP && regs_[r].same(v)) return r;
        return -1;
    }

    // **An address a register holds folds into the operand that uses it.**
    bool foldAddress(Operand &o) const {
        if (o.kind != Operand::Memory || o.reg.id < 0 || o.reg.id == RBP || o.reg.id == RSP) return false;
        const Value &v = regs_[o.reg.id];
        if (v.kind != Value::FrameAddr || !fitsImm32(v.k + o.disp)) return false;
        o.reg = Reg{RBP, 8};
        o.disp += v.k;
        o.hasDisp = true;
        o.text.clear();
        return true;
    }

    // **A known constant becomes an immediate** where the form allows one: into
    // a register always, into memory only where the mnemonic names the width.
    bool immediateSource(Instr &i) const {
        if (!takesImmediate(i.m) || i.operands != 2 || !gpr(i.a)) return false;
        if (gpr(i.b) && i.a.reg.id == i.b.reg.id) return false;     // xor %eax,%eax and the like
        const Value &v = regs_[i.a.reg.id];
        if (v.kind != Value::Const) return false;
        const long long x = atWidth(v.k, i.a.reg.width);
        const bool into = gpr(i.b) || ((i.b.isMem() || i.b.kind == Operand::RipSymbol) && suffixWidth(i.m) != 0);
        if (!fitsImm32(x) || !into) return false;
        i.a = Operand::ofImm(x);
        return true;
    }

    // **A reload of a frame slot whose value a register still holds** is a
    // copy of that register, or of the constant.
    bool reloadFromRegister(Instr &i) const {
        if (!i.a.isMem() || i.a.reg.id != RBP || i.a.scale != 0 || !gpr(i.b) || i.b.reg.width != 8) return false;
        const auto it = slots_.find(i.a.disp);
        if (it == slots_.end()) return false;
        const Slot &slot = it->second;
        const bool whole = isMovQ(i.m) && slot.width == 8;
        const bool extended = i.m == "movslq" && slot.width == 4 &&
                              (slot.v.sext32 || slot.v.kind == Value::Const);
        if (!whole && !extended) return false;
        if (slot.v.kind == Value::Const) {
            const long long x = extended ? sext32(slot.v.k) : slot.v.k;
            if (!fitsImm32(x)) return false;
            i = Instr{"mov", Operand::ofImm(x), i.b, 2};
            return true;
        }
        const int r = regs_[i.b.reg.id].same(slot.v) ? i.b.reg.id : holding(slot.v, -1);
        if (r < 0) return false;
        i = Instr{"mov", Operand::ofReg(r, 8), i.b, 2};
        return true;
    }

    // **`mov $c, %d; add %s, %d` is `lea c(%s), %d`** where nothing reads the
    // flags the add would leave: one instruction, and the constant's move dies.
    bool addressOfSum(Instr &i, int k) const {
        if (i.m != "add" || i.operands != 2 || !gpr(i.a) || !gpr(i.b) || !flagsDead_[k]) return false;
        const int w = i.b.reg.width;
        if (w < 4 || i.a.reg.width != w || i.a.reg.id == i.b.reg.id || frameReg(i.a.reg.id) || frameReg(i.b.reg.id)) return false;
        const Value &v = regs_[i.b.reg.id];
        if (v.kind != Value::Const) return false;
        const long long c = atWidth(v.k, w);
        if (!fitsImm32(c)) return false;
        // A four-byte sum reads its base's low half only, so the base may be
        // what a sign extension was made from, the extension then unread.
        Reg base = i.a.reg;
        if (w == 4) {
            for (int n = 0; n < 4 && original(base); ++n) {}
            const Value &bv = regs_[base.id];
            const auto from = bv.kind == Value::Unknown && bv.sext32 ? sextFrom_.find(bv.id) : sextFrom_.end();
            const int r = from == sextFrom_.end() ? -1 : holding(from->second, -1);
            if (r >= 0) base.id = r;
        }
        if (frameReg(base.id)) return false;
        Operand m = Operand::ofMem(base.id, c);
        m.hasDisp = true;
        m.reg.width = 8;
        i = Instr{"lea", m, i.b, 2};
        return true;
    }

    // **A sign extension another register already holds** is a copy of it.
    bool extensionHeld(Instr &i) const {
        if (i.m != "movslq" || !gpr(i.a) || !gpr(i.b) || i.a.reg.width != 4 || i.b.reg.width != 8) return false;
        const Value &src = regs_[i.a.reg.id];
        if (src.kind != Value::Unknown || src.id == 0 || src.sext32) return false;
        const auto it = sextOf_.find(src.id);
        if (it == sextOf_.end()) return false;
        const int r = holding(it->second, i.b.reg.id);
        if (r < 0 || frameReg(r)) return false;
        i = Instr{"mov", Operand::ofReg(r, 8), i.b, 2};
        return true;
    }

    // **An extension read only at its source's width** - a byte stored, and
    // nothing wider - is a copy of the whole register, which can then go.
    bool extensionUnread(Instr &i, int k) const {
        int ws = 0;
        if (is(i.m, {"movsbq", "movzbq", "movsbl", "movzbl"})) ws = 1;
        else if (is(i.m, {"movswq", "movzwq", "movswl", "movzwl"})) ws = 2;
        if (ws == 0 || !gpr(i.a) || !gpr(i.b) || i.a.reg.width != ws || i.b.reg.width < 4) return false;
        const int d = i.b.reg.id;
        if (frameReg(d) || frameReg(i.a.reg.id)) return false;
        bool redefined = false;
        for (int j = k + 1; j < blk_->end && !redefined; ++j) {
            if (s_[j].kind != Entry::Ins || s_[j].dead) continue;
            const Instr &u = s_[j].ins;
            const Effects &e = f_.effects[j];
            if (!((e.reads | e.writes | e.partial) & bit(d))) continue;
            if (e.partial & bit(d)) return false;
            bool named = false;
            for (const Operand *o : {&u.a, &u.b}) {
                if (o->kind == Operand::Register && o->reg.id == d) {
                    named = true;
                    const bool written = o == &u.b ? (rolesOf(u).b & kWrite) != 0 : (rolesOf(u).a & kWrite) != 0;
                    if (written && o->reg.width >= 4) redefined = true;
                    else if (o->reg.width > ws) return false;
                } else if ((o->isMem() || o->kind == Operand::Indirect) && (o->reg.id == d || (o->indexed() && o->index.id == d))) {
                    return false;
                }
            }
            if (!named || (!redefined && !explicitOnly(u))) return false;
        }
        if (!redefined && (blk_->out.regs & bit(d))) return false;
        i = Instr{"mov", Operand::ofReg(i.a.reg.id, 8), Operand::ofReg(d, 8), 2};
        return true;
    }

    // A copy of what the destination already holds.
    bool isNoop(const Instr &i) const {
        if (!gpr(i.b) || !gpr(i.a)) return false;
        const Value &src = regs_[i.a.reg.id], &dst = regs_[i.b.reg.id];
        if (isMovQ(i.m) && i.a.reg.width == 8 && i.b.reg.width == 8)
            return i.a.reg.id == i.b.reg.id || dst.same(src);
        if (i.m != "movslq" || i.a.reg.width != 4 || i.b.reg.width != 8) return false;
        if (i.a.reg.id == i.b.reg.id && src.sext32) return true;
        // The destination already holds this extension: the same `movslq` again.
        if (src.kind != Value::Unknown || src.id == 0 || src.sext32) return false;
        const auto it = sextOf_.find(src.id);
        return it != sextOf_.end() && dst.same(it->second);
    }

    // **Nothing between a push and its pop may see the stack**: no call, no
    // rsp operand, no push or pop still standing. Then the pair can go.
    bool quiet(int from, int to) const {
        for (int k = from + 1; k < to; ++k) {
            if (s_[k].kind != Entry::Ins || s_[k].dead) continue;
            const Effects &e = f_.effects[k];
            const Instr &i = s_[k].ins;
            if (e.stack || e.opaque || e.control || ((e.reads | e.writes) & bit(RSP))) return false;
            if ((i.a.isMem() && i.a.reg.id == RSP) || (i.b.isMem() && i.b.reg.id == RSP)) return false;
        }
        return true;
    }

    // **A pop whose push is in this block** is a copy from wherever the pushed
    // value still is, and the push goes; the pop too, if it copies nothing.
    Pop pairPop(Instr &i, int k) {
        if (!isPop(i.m) || !gpr(i.a) || i.a.reg.width != 8) return Pop::Kept;
        if (stack_.empty() || !quiet(stack_.back().at, k)) return Pop::Kept;
        const Pop pop = pairWith(i, k, stack_.back(), i.a);
        if (pop != Pop::Kept) stack_.pop_back();
        return pop;
    }

    // **A temporary's load whose store is in this block** is the same pair in
    // a frame slot nothing else reads or writes, and needs no quiet between.
    Pop pairTemp(Instr &i, int k) {
        if (!isTemp(i.a)) return Pop::Kept;
        const auto it = temps_.find(i.a.disp);
        if (it == temps_.end()) return Pop::Kept;
        Pop pop = Pop::Kept;
        if (isMovQ(i.m) && gpr(i.b) && i.b.reg.width == 8) pop = pairWith(i, k, it->second, i.b);
        else if (i.m == "movsd" && xmmReg(i.b)) pop = pairXmm(i, it->second);
        // A load that stays still reads the slot, so no later one may kill the store.
        temps_.erase(it);
        return pop;
    }

    // **A double's pair**: nothing if the register still holds it, a register
    // copy if another does, else through an xmm the function never names.
    Pop pairXmm(Instr &i, const Pushed &p) {
        const int at = p.at, dst = i.b.reg.id;
        if (p.xmm < 0) return Pop::Kept;
        if (untouched(p.xmm, at, k_)) {
            kill(at);
            if (dst == p.xmm) return Pop::Gone;
            i = Instr{"movapd", Operand::ofReg(p.xmm, 16), i.b, 2};
            return Pop::Copy;
        }
        for (int sc : xmmScratch_) {
            if (!untouched(sc, at, k_)) continue;
            replace(at, Instr{"movapd", s_[at].ins.a, Operand::ofReg(sc, 16), 2});
            i = Instr{"movapd", Operand::ofReg(sc, 16), i.b, 2};
            return Pop::Copy;
        }
        return Pop::Kept;
    }
    static bool xmmReg(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= kXmm0 && o.reg.id < kXmm0 + 16; }

    // The pair's second half rewritten as a copy into `dst` and the first killed, or made the copy.
    Pop pairWith(Instr &i, int k, const Pushed &p, const Operand &dstOp) {
        const Value v = p.v;
        const int dst = dstOp.reg.id;
        const int at = p.at;
        std::string m = "mov";
        Operand from;
        if (regs_[dst].same(v)) from = dstOp;
        else if (v.kind == Value::Const && fitsImm32(v.k)) from = Operand::ofImm(v.k);
        else if (holding(v, dst) >= 0) from = Operand::ofReg(holding(v, dst), 8);
        else if (v.kind == Value::FrameAddr) { m = "lea"; from = Operand::ofMem(RBP, v.k); from.hasDisp = true; }
        else if (untouched(dst, at, k) && gpr(s_[at].ins.a)) {
            // Nowhere to copy from now: the push itself becomes the copy.
            replace(at, Instr{"mov", s_[at].ins.a, dstOp, 2});
            writtenBehind(dst, v);
            return Pop::Gone;
        } else if (const int sc = scratchBetween(at, k)) {
            // The push becomes the copy in, the pop the copy out.
            replace(at, Instr{"mov", s_[at].ins.a, Operand::ofReg(sc, 8), 2});
            writtenBehind(sc, v);
            i = Instr{"mov", Operand::ofReg(sc, 8), dstOp, 2};
            return Pop::Copy;
        } else return Pop::Kept;
        kill(at);
        if (from.isReg(dst)) return Pop::Gone;
        i = Instr{m, from, dstOp, 2};
        return Pop::Copy;
    }

    // A scratch register nothing between two entries touches; 0 (rax, never scratch) if none.
    int scratchBetween(int from, int to) const {
        const Operand &pushed = s_[from].ins.a;
        if (pushed.kind == Operand::Register && !gpr(pushed)) return 0;
        for (int r : scratch_)
            if (r != RAX && untouched(r, from, to)) return r;
        return 0;
    }

    // **A register written by an edit behind the walk** - a push made a copy -
    // is written as far as everything known is concerned: its copies go stale.
    void writtenBehind(int r, const Value &v) {
        regs_[r] = v;
        version_[r]++;
        if (condReg_ == r) condReg_ = -1;
    }

    // Whether no instruction between two entries reads or writes register r.
    bool untouched(int r, int from, int to) const {
        for (int k = from + 1; k < to; ++k)
            if (s_[k].kind == Entry::Ins && !s_[k].dead &&
                ((f_.effects[k].reads | f_.effects[k].writes | f_.effects[k].partial) & bit(r)))
                return false;
        return true;
    }

    // **A register read where the value it copied still is** reads that
    // register instead, so the copy can die. Never a shift count, which only
    // %cl can be.
    bool readOriginal(Instr &i) const {
        bool edited = false;
        const bool sourceOnly = i.operands == 2 || isPush(i.m);
        const bool shift = opcodeOf(i.m).has(Opcode::kShift);
        if (sourceOnly && !shift && gpr(i.a)) edited = original(i.a.reg) || edited;
        if (is(i.m, {"cmp", "cmpl", "cmpq", "test", "testl", "testq"}) && gpr(i.b)) edited = original(i.b.reg) || edited;
        for (Operand *o : {&i.a, &i.b}) {
            if (o->kind == Operand::Memory && o->reg.id >= 0 && o->reg.id < kGprs && o->reg.id != RBP && o->reg.id != RSP)
                edited = original(o->reg) || edited;
            if (o->indexed()) edited = original(o->index) || edited;
        }
        return edited;
    }
    bool original(Reg &r) const {
        const Copy &c = copies_[r.id];
        if (c.from < 0 || version_[c.from] != c.fromVersion || version_[r.id] != c.toVersion || r.width > c.width)
            return false;
        r.id = c.from;
        return true;
    }

    // **setcc, a zero extension, and a compare of it against zero** ask again
    // what the flags already say: the jump that follows tests them directly.
    bool foldCondition(const Instr &i, int k) {
        if (!is(i.m, {"cmp", "cmpq", "test", "testq"}) || !gpr(i.b)) return false;
        const bool againstZero = i.m[0] == 'c' ? i.a.kind == Operand::Immediate && i.a.numeric && i.a.value == 0
                                               : i.a.isReg(i.b.reg.id);
        const Value &v = regs_[i.b.reg.id];
        if (!againstZero || v.kind != Value::Condition || v.flagsFrom != flagsFrom_ || flagsFrom_ < 0) return false;
        int j = k + 1;
        while (j < static_cast<int>(s_.size()) && s_[j].kind != Entry::Label &&
               (s_[j].kind != Entry::Ins || s_[j].dead)) ++j;
        if (j == static_cast<int>(s_.size()) || s_[j].kind != Entry::Ins) return false;
        const std::string cc = conditionOf(s_[j].ins.m);
        if (s_[j].ins.m[0] != 'j' || !is(cc, {"e", "ne", "z", "nz"})) return false;
        Instr jump = s_[j].ins;
        jump.m = "j" + (cc == "e" || cc == "z" ? inverse(v.cc) : v.cc);
        replace(j, jump);
        return true;
    }

    // **The instruction's effect on what is known**, once it is rewritten.
    void learn(const Instr &i, int k) {
        const Effects &e = f_.effects[k];
        const std::string &m = i.m;
        const bool extendsCond = is(m, {"movzbq", "movzbl"}) && i.a.isReg(condReg_) && i.b.isReg(condReg_);
        if (condReg_ >= 0 && !extendsCond && ((e.writes | e.partial) & bit(condReg_))) condReg_ = -1;
        for (int r = 0; r < kGprs; ++r) if ((e.writes | e.partial) & bit(r)) version_[r]++;
        if (e.flagsWritten) flagsFrom_ = k;

        if (e.opaque || e.control) {
            forget(e.writes | e.partial);
            if (e.memoryWritten) slots_.clear();
            if (e.opaque) stack_.clear();
            return;
        }
        if (isPush(m)) {
            Value v = fresh();
            if (gpr(i.a) && i.a.reg.width == 8) v = regs_[i.a.reg.id];
            else if (i.a.kind == Operand::Immediate && i.a.numeric) v = Value::constant(i.a.value);
            stack_.push_back(Pushed{v, k});
            return;
        }
        if (isPop(m)) {
            Value v = fresh();
            if (!stack_.empty()) { v = stack_.back().v; stack_.pop_back(); }
            if (gpr(i.a)) regs_[i.a.reg.id] = i.a.reg.width == 8 ? v : fresh();
            else if (e.memoryWritten) slots_.clear();
            return;
        }
        if (e.stack || (e.writes & bit(RSP))) stack_.clear();
        if (e.memoryWritten) store(i);
        if (isMovQ(i.m) && isTemp(i.b) && i.operands == 2) {
            Value v = fresh();
            if (gpr(i.a) && i.a.reg.width == 8) v = regs_[i.a.reg.id];
            else if (i.a.kind == Operand::Immediate && i.a.numeric) v = Value::constant(i.a.value);
            temps_[i.b.disp] = Pushed{v, k};
        } else if (i.m == "movsd" && isTemp(i.b) && xmmReg(i.a)) {
            temps_[i.b.disp] = Pushed{fresh(), k, i.a.reg.id};
        }

        if (!gpr(i.b)) {
            forget(e.writes | e.partial);
            const std::string cc = conditionOf(m);
            if (!cc.empty() && m[0] == 's' && gpr(i.a)) setCondition(i.a.reg.id, cc);
            return;
        }
        const int d = i.b.reg.id;
        const Value out = result(i, extendsCond);
        const int w = i.b.reg.width;
        // A sign extension copies the low four bytes.
        const bool whole = isMovQL(m) && i.a.reg.width == w && w >= 4;
        const bool low = m == "movslq" && i.a.reg.width == 4;
        copies_[d] = gpr(i.a) && i.a.reg.id != d && (whole || low)
                         ? Copy{i.a.reg.id, low ? 4 : w, version_[i.a.reg.id], version_[d]} : Copy();
        forget((e.writes | e.partial) & ~bit(d));
        regs_[d] = (i.b.reg.width >= 4 || out.kind == Value::Condition) ? out : fresh();
    }

    void forget(RegSet regs) {
        for (int r = 0; r < kGprs; ++r) if (regs & bit(r)) regs_[r] = fresh();
    }

    void setCondition(int r, const std::string &cc) {
        condReg_ = r;
        cond_ = Value();
        cond_.kind = Value::Condition;
        cond_.cc = cc;
        cond_.flagsFrom = flagsFrom_;
        cond_.sext32 = true;
    }

    // A store into the frame is remembered; any other forgets the frame.
    void store(const Instr &i) {
        if (!i.b.isMem() || i.b.reg.id != RBP || i.b.scale != 0) { slots_.clear(); return; }
        const int w = suffixWidth(i.m) ? suffixWidth(i.m) : i.a.kind == Operand::Register ? i.a.reg.width : 16;
        for (auto it = slots_.begin(); it != slots_.end();) {
            const bool overlaps = it->first < i.b.disp + w && i.b.disp < it->first + it->second.width;
            it = overlaps ? slots_.erase(it) : std::next(it);
        }
        if (!isMovQL(i.m) || (w != 8 && w != 4)) return;
        Value v = fresh();
        if (i.a.kind == Operand::Immediate && i.a.numeric) v = Value::constant(atWidth(i.a.value, w));
        else if (gpr(i.a)) v = regs_[i.a.reg.id];
        slots_[i.b.disp] = Slot{w, v};
    }

    // What an instruction writing register b leaves in it.
    Value result(const Instr &i, bool extendsCond) {
        const std::string &m = i.m;
        const int w = i.b.reg.width;
        const Value src = gpr(i.a) ? regs_[i.a.reg.id] : Value();
        const bool constSrc = i.a.kind == Operand::Immediate && i.a.numeric;
        if ((isMovQ(m) || m == "movabs") && w == 8) {
            if (constSrc) return Value::constant(i.a.value);
            if (gpr(i.a) && i.a.reg.width == 8) return src;
        } else if (is(m, {"mov", "movl"}) && w == 4) {
            if (constSrc) return Value::constant(i.a.value & 0xffffffffLL);
            if (src.kind == Value::Const) return Value::constant(src.k & 0xffffffffLL);
        } else if (m == "movslq") {
            if (gpr(i.a) && src.kind == Value::Const) return Value::constant(sext32(src.k));
            if (gpr(i.a) && src.sext32) return src;
            Value v = fresh();
            v.sext32 = true;
            if (gpr(i.a) && src.kind == Value::Unknown && src.id != 0) {
                const auto it = sextOf_.find(src.id);
                if (it != sextOf_.end()) return it->second;
                sextOf_[src.id] = v;
                sextFrom_[v.id] = src;
            }
            return v;
        } else if (extendsCond) {
            return cond_;
        } else if (is(m, {"movzbq", "movzbl"}) && src.kind == Value::Const) {
            return Value::constant(src.k & 0xff);
        } else if (is(m, {"movsbq", "movsbl"}) && src.kind == Value::Const) {
            return Value::constant(atWidth(src.k, 1));
        } else if (is(m, {"movswq", "movswl"}) && src.kind == Value::Const) {
            return Value::constant(atWidth(src.k, 2));
        } else if (is(m, {"movzwq", "movzwl"}) && src.kind == Value::Const) {
            return Value::constant(src.k & 0xffff);
        } else if (m == "lea" && i.a.isMem() && i.a.reg.id == RBP && i.a.scale == 0) {
            return Value::frame(i.a.disp);
        } else if (is(m, {"add", "sub"}) && constSrc && w == 8) {
            const Value &cur = regs_[i.b.reg.id];
            const long long delta = m == "add" ? i.a.value : -i.a.value;
            if (cur.kind == Value::Const) return Value::constant(cur.k + delta);
            if (cur.kind == Value::FrameAddr) return Value::frame(cur.k + delta);
        }
        return fresh();
    }
};

}

bool forwardValues(Stream &s, Flow &f, const Convention &c, long long tempFrom) {
    return Forward(s, f, c, tempFrom).run();
}

}
