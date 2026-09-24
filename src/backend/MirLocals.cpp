#include "MirLocals.h"

#include "OptPasses.h"

#include <map>

namespace mir {

using namespace opt;

// The candidates are promoteLocals' own - not shared, never addressed,
// every access whole and by an instruction that takes a register there.
int Locals::promote() {
    RegSet mentioned = 0;
    const std::vector<Local> found = promotableLocals(fn_.stream, fn_.locals, fn_.shared, mentioned);
    if (found.empty()) return 0;
    std::map<long long, int> pseudoOf;
    for (const Local &l : found) {
        pseudoOf[l.disp] = kFirstPseudo + static_cast<int>(fn_.homes.size() + fn_.slots.size());
        fn_.slots.push_back(l);
    }
    for (int k = 0; k < static_cast<int>(fn_.stream.size()); ++k) {
        Entry &e = fn_.stream[k];
        if (e.kind != Entry::Ins || e.dead) continue;
        bool renamed = false;
        for (Operand *o : {&e.ins.a, &e.ins.b}) {
            if (o->kind != Operand::Memory || o->reg.id != RBP) continue;
            const auto p = pseudoOf.find(o->disp);
            if (p == pseudoOf.end()) continue;
            *o = Operand::ofReg(p->second, fn_.slots[p->second - kFirstPseudo - fn_.homes.size()].size);
            renamed = true;
        }
        if (renamed) fn_.flow.effects[k] = effectsOf(e.ins, fn_.convention);
    }
    fn_.flow.touch();
    fn_.promoted = true;
    return static_cast<int>(found.size());
}

}
