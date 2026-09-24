#include "OptFunction.h"
#include "OptPasses.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }

// An operand's register slots: the register (or an address's base) and the index.
Reg *slotOf(Instr &i, int slot) {
    Operand &o = slot < 2 ? i.a : i.b;
    if (slot & 1) return o.indexed() ? &o.index : nullptr;
    return (o.kind == Operand::Register || o.kind == Operand::Memory || o.kind == Operand::Indirect) && o.reg.id >= 0 &&
                   o.reg.id < kGprs
               ? &o.reg
               : nullptr;
}
const Reg *slotOf(const Instr &i, int slot) { return slotOf(const_cast<Instr &>(i), slot); }
int slotFor(int operand, bool index) { return operand * 2 + (index ? 1 : 0); }   // 0 a, 1 a's index, 2 b, 3 b's index

std::string desc(long long v) { return std::to_string(v); }

// **One loop's plan**: what is hoisted, what is replaced by a value already
// hoisted, and which reads take the register that value is given.
class Hoister {
public:
    Hoister(Function &fn, const Loops::Loop &loop) : fn_(fn), s_(fn.stream), f_(fn.flow), loop_(loop) {}

    // Whether the loop changed; the stream and the flow are then stale.
    bool run() {
        if (!preheader()) return false;
        f_.live(s_);
        if (!invariants()) return false;
        for (int b : loop_.blocks) {
            blockStarts_.push_back(static_cast<int>(seq_.size()));
            scanBlock(b);
        }
        // A value with no register takes its whole block's plan with it: no
        // chain crosses a block, so what is left stands on its own.
        while (!seq_.empty() && !assign()) {
            while (!blockStarts_.empty() && blockStarts_.back() >= static_cast<int>(seq_.size())) blockStarts_.pop_back();
            seq_.resize(blockStarts_.empty() ? 0 : blockStarts_.back());
        }
        if (seq_.empty()) return false;
        apply();
        return true;
    }

private:
    enum SeqKind { Item, Replaced };
    // A step of the plan: hoisted (an Item, defining vn) or deleted for a value held; narrow: a four-byte copy.
    struct Step { SeqKind kind; int k; int vn; int srcVn; bool narrow; std::vector<std::pair<int, int>> sources; };
    // A read renamed to a value's register: the entry, its slot, the value, the step that defined it.
    struct Rename { int k; int slot; int vn; int definer; };
    struct Holder { int reg = -1; int step = -1; };    // an invariant register, or the step that computes it

    Function &fn_;
    Stream &s_;
    Flow &f_;
    const Loops::Loop &loop_;
    std::vector<bool> inLoop_;
    int pos_ = -1;                       // the entry the hoisted instructions go in front of
    int preBlock_ = -1;
    RegSet written_ = 0, touched_ = 0, avail_ = 0;
    int availCount_ = 0;
    std::set<int> carried_;              // the values read in the loop: each needs a register of avail_
    Live headIn_;
    std::vector<bool> flagsDeadAfter_;
    std::map<std::string, int> vnOf_;
    std::vector<Holder> holders_;
    std::vector<Step> seq_;
    std::vector<int> blockStarts_;       // where each block's steps begin in seq_
    std::vector<Rename> renames_;
    std::set<int> refused_;              // entries a failed block plan blacklisted
    bool narrowSrc_ = false;             // makeStep: whether the copy's source is itself a narrow value
    std::vector<int> assigned_;          // per step: the register its value takes
    std::vector<bool> copyFirst_;        // per step: an RMW whose source sits in another register

    // **The one edge into the head from outside the loop**, falling through
    // or jumping: the hoisted code goes just before the head's label, or
    // before the jump. Every other block's predecessors are the loop's own.
    bool preheader() {
        inLoop_.assign(f_.blocks.size(), false);
        for (int b : loop_.blocks) inLoop_[b] = true;
        int entries = 0;
        for (int e : f_.blocks[loop_.head].preds) {
            const Edge &edge = f_.edges[e];
            if (inLoop_[edge.from]) continue;
            if (edge.kind == Edge::Eh) return false;
            ++entries;
            preBlock_ = edge.from;
            if (edge.kind == Edge::Fallthrough) pos_ = f_.blocks[loop_.head].begin;
            else if (edge.kind == Edge::Jump) pos_ = f_.blocks[edge.from].end - 1;
            else return false;
        }
        if (entries != 1 || f_.blocks[preBlock_].leaves) return false;
        if (pos_ < 0 || (s_[pos_].kind == Entry::Ins && s_[pos_].ins.m != "jmp")) return false;
        for (int b : loop_.blocks) {
            if (b == loop_.head) continue;
            for (int e : f_.blocks[b].preds)
                if (!inLoop_[f_.edges[e].from]) return false;
        }
        return true;
    }

    // What the loop writes and touches, and where the flags are dead after
    // each instruction; false where something in it is opaque.
    bool invariants() {
        flagsDeadAfter_.assign(s_.size(), false);
        headIn_ = f_.blocks[loop_.head].in;
        for (int b : loop_.blocks) {
            const Block &blk = f_.blocks[b];
            if (blk.leaves) return false;
            Live live = blk.out;
            for (int k = blk.end - 1; k >= blk.begin; --k) {
                if (s_[k].kind != Entry::Ins || s_[k].dead) continue;
                f_.joinPads(b, k, live);
                flagsDeadAfter_[k] = !live.flags;
                live.step(f_.effects[k]);
                const Effects &e = f_.effects[k];
                if (e.opaque) return false;
                written_ |= e.writes | e.partial;
                touched_ |= e.reads | e.writes | e.partial;
            }
        }
        for (int r = 0; r < kGprs; ++r)
            if (!frameReg(r) && !(headIn_.regs & bit(r)) && !(touched_ & bit(r))) { avail_ |= bit(r); ++availCount_; }
        return availCount_ > 0;
    }

    int vnOfReg(int r) { return vnOfKey("r" + desc(r), r, -1); }
    int vnOfKey(const std::string &key, int reg, int step) {
        const auto it = vnOf_.find(key);
        if (it != vnOf_.end()) return it->second;
        const int vn = static_cast<int>(holders_.size());
        vnOf_[key] = vn;
        Holder h;
        h.reg = reg;
        h.step = step;
        holders_.push_back(h);
        return vn;
    }

    // **One block, top down**, each register carrying the value it holds
    // where that value is invariant; a plan that fails is made again
    // without the instruction it failed on.
    void scanBlock(int b) {
        for (;;) {
            const std::size_t seqAt = seq_.size(), renAt = renames_.size();
            const std::size_t vnAt = holders_.size();
            const std::set<int> carriedAt = carried_;
            const int failed = tryBlock(b);
            if (failed < 0) return;
            carried_ = carriedAt;
            seq_.resize(seqAt);
            renames_.resize(renAt);
            holders_.resize(vnAt);
            for (auto it = vnOf_.begin(); it != vnOf_.end();)
                it = it->second >= static_cast<int>(vnAt) ? vnOf_.erase(it) : std::next(it);
            refused_.insert(failed);
        }
    }

    // The entry the plan failed on, or -1 where the block's plan holds.
    int tryBlock(int b) {
        const Block &blk = f_.blocks[b];
        int curVn[kGprs], definer[kGprs];
        for (int r = 0; r < kGprs; ++r) {
            curVn[r] = (written_ & bit(r)) ? -1 : vnOfReg(r);
            definer[r] = -1;
        }
        for (int k = blk.begin; k < blk.end; ++k) {
            if (s_[k].kind != Entry::Ins || s_[k].dead) continue;
            const Instr &i = s_[k].ins;
            const Effects &e = f_.effects[k];
            const Roles roles = rolesOf(i);
            // The registers this instruction names, and whether it reads the
            // destination as well as writing it.
            RegSet named = 0;
            for (int slot = 0; slot < 4; ++slot) {
                const Reg *r = slotOf(i, slot);
                if (r != nullptr) named |= bit(r->id);
            }
            const bool rmw = i.operands == 2 && gpr(i.b) && (roles.b & kRead) && (roles.b & kWrite);
            const int dst = i.operands == 2 && gpr(i.b) && (roles.b & kWrite) ? i.b.reg.id : -1;
            // A value the plan defined, read where no rename can reach it.
            for (int r = 0; r < kGprs; ++r) {
                if (definer[r] < 0) continue;
                const bool implicit = (e.reads & bit(r)) && !(named & bit(r));
                const bool count = opcodeOf(i.m).has(Opcode::kShift) && i.a.kind == Operand::Register && i.a.reg.id == r;
                const bool clobbered = (e.partial & bit(r)) || ((e.writes & bit(r)) && r != dst);
                if (implicit || count || clobbered) return seq_[definer[r]].k;
            }
            const bool hoist = hoistable(k, i, e, roles, curVn, definer);
            if (!hoist && rmw && definer[dst] >= 0) return seq_[definer[dst]].k;
            if (hoist) {
                narrowSrc_ = gpr(i.a) && definer[i.a.reg.id] >= 0 && seq_[definer[i.a.reg.id]].narrow;
                Step step = makeStep(k, i, roles, curVn);
                seq_.push_back(step);
                curVn[dst] = step.vn;
                definer[dst] = static_cast<int>(seq_.size()) - 1;
                continue;
            }
            // A read of a planned value takes its register: a narrow one only
            // at four bytes, and one the loop reads only while a register is free.
            for (int slot = 0; slot < 4; ++slot) {
                const Reg *r = slotOf(i, slot);
                if (r == nullptr || definer[r->id] < 0) continue;
                if (slot == 2 && i.b.kind == Operand::Register && (roles.b & kWrite)) continue;   // written, not read
                const Step &def = seq_[definer[r->id]];
                if (def.narrow && !readsNarrow(i, slot)) return def.k;
                const int vn = curVn[r->id];
                if (holders_[vn].reg < 0 && !carried_.count(vn)) {
                    if (static_cast<int>(carried_.size()) >= availCount_) return def.k;
                    carried_.insert(vn);
                }
                renames_.push_back(Rename{k, slot, vn, definer[r->id]});
            }
            for (int r = 0; r < kGprs; ++r)
                if ((e.writes | e.partial) & bit(r)) { curVn[r] = -1; definer[r] = -1; }
        }
        for (int r = 0; r < kGprs; ++r)
            if (definer[r] >= 0 && (blk.out.regs & bit(r))) return seq_[definer[r]].k;
        return -1;
    }

    // **What may leave the loop**: two operands, a whole general register
    // written, nothing implicit, no memory, no flags read, the flags dead
    // after where it writes them, and every register it reads invariant.
    static bool readsNarrow(const Instr &i, int slot) {
        const Operand &o = slot < 2 ? i.a : i.b;
        return !(slot & 1) && o.kind == Operand::Register && o.reg.width <= 4;
    }

    bool hoistable(int k, const Instr &i, const Effects &e, const Roles &roles, const int *curVn, const int *definer) const {
        if (refused_.count(k) || i.operands != 2 || !gpr(i.b) || i.b.reg.width < 4 || frameReg(i.b.reg.id)) return false;
        if (!(roles.b & kWrite) || (roles.b & kKeep) || !explicitOnly(i)) return false;
        if (e.control || e.opaque || e.stack || e.memoryRead || e.memoryWritten || e.flagsRead) return false;
        if (e.flagsWritten && (!flagsDeadAfter_[k] || headIn_.flags)) return false;
        const Opcode &op = opcodeOf(i.m);
        if (op.kind != Opcode::Move && op.kind != Opcode::Rmw) return false;
        // A constant is an immediate wherever one is taken, a zeroing is
        // free, and a frame address folds into the operand: none is worth a register.
        switch (i.a.kind) {
        case Operand::Register: if (!gpr(i.a) || !(roles.a & kRead)) return false; break;
        case Operand::Immediate: if (op.kind == Opcode::Move) return false; break;
        case Operand::RipSymbol: break;
        case Operand::Memory: if (!(roles.a & kAddress) || frameReg(i.a.reg.id)) return false; break;
        default: return false;
        }
        for (int r = 0; r < kGprs; ++r)
            if ((e.reads & bit(r)) && curVn[r] < 0) return false;
        for (int slot = 0; slot < 4; ++slot) {
            const Reg *r = slotOf(i, slot);
            if (r != nullptr && definer[r->id] >= 0 && seq_[definer[r->id]].narrow && !readsNarrow(i, slot)) return false;
        }
        return true;
    }

    // The step for a hoistable instruction: its value's number from its
    // mnemonic, its immediate and its sources' numbers, so one computed
    // twice is held once.
    Step makeStep(int k, const Instr &i, const Roles &roles, const int *curVn) {
        Step step;
        step.kind = Item;
        step.k = k;
        step.srcVn = -1;
        step.narrow = false;
        std::string key = i.m + "|w" + desc(i.b.reg.width) + "|";
        switch (i.a.kind) {
        case Operand::Register:
            if (roles.a & kRead) {
                key += "r" + desc(curVn[i.a.reg.id]) + "." + desc(i.a.reg.width);
                step.sources.push_back(std::make_pair(slotFor(0, false), curVn[i.a.reg.id]));
            }
            break;
        case Operand::Immediate: key += "i" + (i.a.numeric ? desc(i.a.value) : i.a.text); break;
        case Operand::RipSymbol: key += "s" + i.a.text; break;
        case Operand::Memory:
            key += "m" + desc(i.a.disp) + ",";
            if (i.a.reg.id >= 0) {
                key += desc(curVn[i.a.reg.id]);
                step.sources.push_back(std::make_pair(slotFor(0, false), curVn[i.a.reg.id]));
            }
            if (i.a.indexed()) {
                key += "," + desc(curVn[i.a.index.id]) + "*" + desc(i.a.scale);
                step.sources.push_back(std::make_pair(slotFor(0, true), curVn[i.a.index.id]));
            }
            break;
        default: break;
        }
        if (roles.b & kRead) {
            step.srcVn = curVn[i.b.reg.id];
            key += "|b" + desc(step.srcVn);
        }
        // A copy of an invariant is that invariant, wherever it is held; a
        // four-byte one only for what reads it at four bytes.
        if (isMovQL(i.m) && gpr(i.a) && i.a.reg.width == i.b.reg.width && i.a.reg.id != i.b.reg.id) {
            step.kind = Replaced;
            step.vn = curVn[i.a.reg.id];
            step.narrow = i.b.reg.width == 4 || narrowSrc_;
            step.sources.clear();
            return step;
        }
        const auto have = vnOf_.find(key);
        if (have != vnOf_.end()) {
            step.kind = Replaced;
            step.vn = have->second;
            return step;
        }
        step.vn = vnOfKey(key, -1, static_cast<int>(seq_.size()));
        return step;
    }

    // Whether the value's holder is an invariant register the loop already has.
    int registerOf(int vn) const {
        const Holder &h = holders_[vn];
        return h.reg >= 0 ? h.reg : assigned_[h.step];
    }

    // **A register for each hoisted value**: one of avail_ where the loop
    // reads it, else any dead at the head until its last hoisted reader.
    bool assign() {
        const int n = static_cast<int>(seq_.size());
        // A rename whose definer is gone reverts, and a step reading a dropped value cannot stand.
        renames_.erase(std::remove_if(renames_.begin(), renames_.end(), [&](const Rename &r) { return r.definer >= n; }), renames_.end());
        std::vector<bool> carried(holders_.size(), false);
        std::vector<int> lastUse(holders_.size(), -1);
        for (const Rename &r : renames_) carried[r.vn] = true;
        for (int st = 0; st < n; ++st) {
            if (seq_[st].kind == Replaced) continue;
            for (const auto &src : seq_[st].sources) lastUse[src.second] = st;
            if (seq_[st].srcVn >= 0) lastUse[seq_[st].srcVn] = st;
        }
        const RegSet avail = avail_;
        RegSet deadAtHead = 0;
        for (int r = 0; r < kGprs; ++r)
            if (!frameReg(r) && !(headIn_.regs & bit(r))) deadAtHead |= bit(r);
        assigned_.assign(n, -1);
        copyFirst_.assign(n, false);
        RegSet inUse = 0;
        std::vector<int> regOf(holders_.size(), -1);
        for (int st = 0; st < n; ++st) {
            const Step &step = seq_[st];
            for (std::size_t vn = 0; vn < holders_.size(); ++vn)
                if (holders_[vn].reg < 0 && regOf[vn] >= 0 && !carried[vn] && lastUse[vn] < st) {
                    inUse &= ~bit(regOf[vn]);
                    regOf[vn] = -1;
                }
            if (step.kind == Replaced) continue;
            int reg = -1;
            if (step.srcVn >= 0 && holders_[step.srcVn].reg < 0 && lastUse[step.srcVn] == st && !carried[step.srcVn] &&
                (!carried[step.vn] || (avail & bit(regOf[step.srcVn])))) {
                reg = regOf[step.srcVn];
                regOf[step.srcVn] = -1;
            } else {
                const RegSet pool = (carried[step.vn] ? avail : deadAtHead) & ~inUse;
                for (int r = 0; r < kGprs && reg < 0; ++r)
                    if ((pool & bit(r)) && (carried[step.vn] || !(avail & bit(r)))) reg = r;
                for (int r = 0; r < kGprs && reg < 0; ++r)
                    if (pool & bit(r)) reg = r;
                if (reg < 0) return false;
                copyFirst_[st] = step.srcVn >= 0 && registerOfNow(step.srcVn, regOf) != reg;
            }
            assigned_[st] = reg;
            regOf[step.vn] = reg;
            inUse |= bit(reg);
        }
        return true;
    }
    int registerOfNow(int vn, const std::vector<int> &regOf) const {
        return holders_[vn].reg >= 0 ? holders_[vn].reg : regOf[vn];
    }

    // **The plan made real**: the hoisted instructions before the head, each
    // with its registers; the loop's copies of them dead; the reads renamed.
    void apply() {
        std::vector<Entry> pre;
        for (std::size_t st = 0; st < seq_.size(); ++st) {
            const Step &step = seq_[st];
            s_[step.k].dead = true;
            if (step.kind == Replaced) continue;
            Instr i = s_[step.k].ins;
            for (const auto &src : step.sources)
                if (Reg *r = slotOf(i, src.first)) r->id = registerOf(src.second);
            i.b.reg.id = assigned_[st];
            if (copyFirst_[st]) {
                Entry c;
                c.ins = Instr{"mov", Operand::ofReg(registerOf(step.srcVn), 8), Operand::ofReg(assigned_[st], 8), 2};
                pre.push_back(c);
            }
            Entry e;
            e.ins = i;
            pre.push_back(e);
        }
        for (const Rename &r : renames_)
            if (Reg *reg = slotOf(s_[r.k].ins, r.slot)) reg->id = registerOf(r.vn);
        s_.insert(s_.begin() + pos_, pre.begin(), pre.end());
    }
};

}

// **Loop-invariant code motion, after allocation**: a computation of what the
// loop never writes moves in front of its head, into a register free there;
// the innermost loop first, the flow rebuilt after each one that changed.
bool hoistInvariants(Function &fn) {
    bool changed = false;
    std::set<std::string> done;
    for (;;) {
        Flow &f = fn.flow;
        const Loops &loops = fn.loops();
        const Loops::Loop *pick = nullptr;
        for (const Loops::Loop &l : loops.all()) {
            const Entry &head = fn.stream[f.blocks[l.head].begin];
            if (head.kind != Entry::Label || done.count(head.label)) continue;
            if (pick == nullptr || l.blocks.size() < pick->blocks.size()) pick = &l;
        }
        if (pick == nullptr) break;
        done.insert(fn.stream[f.blocks[pick->head].begin].label);
        Hoister h(fn, *pick);
        if (!h.run()) continue;
        changed = true;
        fn.buildFlow();
    }
    return changed;
}

}
