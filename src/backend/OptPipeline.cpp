#include "OptPipeline.h"

#include "Mir.h"
#include "MirAlloc.h"
#include "MirLocals.h"
#include "OptPasses.h"

#include <cassert>

namespace opt {

// **The passes, each a Pass over the function.** Every one of these does
// what the function it calls did before the manager existed, and nothing
// more; the manager's rules replace the driver's hand-written loops.
namespace {

// A property set that says "the flow graph must describe the stream".
constexpr unsigned kFlow = kPropFlow;

struct ForwardValues : Pass {
    ForwardValues() : Pass(PassInfo{"forward-values", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return forwardValues(fn.stream, fn.flow, fn.convention, fn.tempFrom); }
};

struct RemoveUnreachable : Pass {
    RemoveUnreachable() : Pass(PassInfo{"remove-unreachable", 0, 0, 0, 0}) {}
    bool execute(Function &fn) override { return removeUnreachable(fn.stream); }
};

struct RemoveDead : Pass {
    RemoveDead() : Pass(PassInfo{"remove-dead", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return removeDead(fn.stream, fn.flow); }
};

struct CoalesceCopies : Pass {
    CoalesceCopies() : Pass(PassInfo{"coalesce-copies", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return coalesceCopies(fn.stream, fn.flow, fn.convention); }
};

struct FoldLoads : Pass {
    FoldLoads() : Pass(PassInfo{"fold-loads", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return foldLoads(fn.stream, fn.flow, fn.convention); }
};

struct FoldOffsets : Pass {
    FoldOffsets() : Pass(PassInfo{"fold-offsets", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return foldOffsets(fn.stream, fn.flow, fn.convention); }
};

struct ThreadJumps : Pass {
    ThreadJumps() : Pass(PassInfo{"thread-jumps", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return threadJumps(fn); }
};

// Ten instructions for three, so a speed level's alone; the flow is rebuilt
// before anything reads the entries this inserts.
struct DivideByConstant : Pass {
    DivideByConstant() : Pass(PassInfo{"divide-by-constant", kFlow, 0, 0, 0}) {}
    bool gate(const Function &fn) const override { return !fn.costs().forSize(); }
    bool execute(Function &fn) override { return divideByConstant(fn.stream, fn.flow); }
};

// **Loop-invariant code motion**, after allocation, where a register the
// loop never touches can be seen to be free; a speed level's alone.
struct HoistInvariants : Pass {
    HoistInvariants() : Pass(PassInfo{"hoist-invariants", kFlow | kPropPhysical, 0, 0, kTodoBuildFlow}) {}
    bool gate(const Function &fn) const override { return fn.whole && !fn.costs().forSize(); }
    bool execute(Function &fn) override { return hoistInvariants(fn); }
};

// **Scaled-index addressing**, after allocation so that webs and the
// allocator never meet an indexed operand; `imul $8` is still a multiply here.
struct FoldIndex : Pass {
    FoldIndex() : Pass(PassInfo{"fold-index", kFlow | kPropPhysical, 0, 0, kTodoBuildFlow}) {}
    bool execute(Function &fn) override { return foldIndex(fn.stream, fn.flow, fn.convention); }
};

// **The pinned occurrences split off with copies, every web a pseudo**, the
// register each was found in kept as its home for the allocator. The flow
// follows the inserted entries and the renaming; the stream names pseudos now.
struct Webs : Pass {
    Webs() : Pass(PassInfo{"webs", kFlow | kPropPhysical, 0, kPropPhysical, kTodoBuildFlow}) {}
    bool execute(Function &fn) override {
        mir::Webs webs(fn);
        const bool split = webs.splitPinned();
        webs.build();
        fn.homes = webs.homes();
        return split;
    }
};

// **Every scalar local whose slot a register could hold becomes a pseudo**,
// the slot kept for the allocator to give back.
struct LocalsPass : Pass {
    LocalsPass() : Pass(PassInfo{"locals", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return mir::Locals(fn).promote() > 0; }
};

// **Every pseudo given a register**, or its slot back, and the copies that
// then copy a register to itself dropped. The restores of the preserved
// registers it took leave the flow describing the stream no longer.
struct Allocate : Pass {
    Allocate() : Pass(PassInfo{"allocate", kFlow, kPropPhysical, kFlow, 0}) {}
    bool execute(Function &fn) override {
        mir::Allocator alloc(fn);
        const bool changed = alloc.run();
        fn.homes.clear();
        fn.slots.clear();
        return changed;
    }
};

struct RemoveDeadStores : Pass {
    RemoveDeadStores() : Pass(PassInfo{"remove-dead-stores", 0, 0, 0, 0}) {}
    bool execute(Function &fn) override { return removeDeadStores(fn.stream, fn.shared); }
};

// **The temporaries' region shrinks to the slots still named**: the passes
// resolve most into registers, and a frame reserved for the rest moves every
// rendered displacement on Windows.
static void shrinkTemporaries(Function &fn) {
    if (fn.tempCount == 0) return;
    int used = 0;
    for (const Entry &e : fn.stream) {
        if (e.kind != Entry::Ins || e.dead) continue;
        for (const Operand *o : {&e.ins.a, &e.ins.b}) {
            if (!o->isMem() || o->reg.id != RBP || o->scale != 0 || o->disp > fn.tempFrom) continue;
            const long long t = (-o->disp - fn.tempBase) / 8;
            if (t <= fn.tempCount && t > used) used = static_cast<int>(t);
        }
    }
    fn.inlineTop = static_cast<int>((fn.tempBase + 8 * used + 15) & ~15);
}

// **The frame as the passes leave it**: the saves nothing needs any more
// dropped, and the prologue rewritten with the rest and the final size.
struct FinishFrame : Pass {
    FinishFrame() : Pass(PassInfo{"finish-frame", 0, 0, 0, 0}) {}
    bool execute(Function &fn) override {
        const bool had = !fn.saves.empty();
        shrinkTemporaries(fn);
        dropUnusedSaves(fn.stream, fn.saves, fn.frameBase());
        fn.size = fn.frameBase() + ((8 * static_cast<int>(fn.saves.size()) + 15) & ~15);
        if (fn.size != fn.frameSize) {
            assert(fn.prologueAt >= 0 && "a frame can grow only while its prologue is held");
            const std::vector<SavedReg> saves = fn.saves;
            const int size = fn.size;
            const std::string lsda = fn.lsda;
            const int outgoing = fn.outgoing;
            fn.stream[fn.prologueAt].event = [=](Spelling &sp) { sp.calleeSaves(saves); sp.prologue(size, lsda, outgoing); };
        }
        return had;
    }
};

struct Shrink : Pass {
    Shrink() : Pass(PassInfo{"shrink", kFlow, 0, 0, 0}) {}
    bool execute(Function &fn) override { return shrink(fn.stream, fn.flow, fn.convention); }
};

// The frame passes run only on a whole function - a funclet's frame is its
// parent's - while its prologue is still held.
struct FrameGroup : Group {
    FrameGroup() : Group(PassInfo{"frame", 0, 0, 0, 0}, 1, WhenNoneChanged) {}
    bool gate(const Function &fn) const override { return fn.whole && fn.prologueAt >= 0; }
};

// Everything again once locals have registers - if any left its slot.
struct RoundsIfPromoted : Group {
    RoundsIfPromoted() : Group(PassInfo{"rounds", 0, 0, 0, kTodoDropLabels | kTodoBuildFlow}, 0, WhenNoneChanged) {}
    bool gate(const Function &fn) const override { return fn.promoted || !fn.saves.empty(); }
};

template <class G>
std::unique_ptr<Group> rounds() {
    std::unique_ptr<Group> g(new G());
    g->add(std::unique_ptr<Pass>(new ForwardValues()));
    g->add(std::unique_ptr<Pass>(new RemoveUnreachable()));
    g->add(std::unique_ptr<Pass>(new RemoveDead()));
    g->add(std::unique_ptr<Pass>(new CoalesceCopies()));
    g->add(std::unique_ptr<Pass>(new FoldLoads()));
    g->add(std::unique_ptr<Pass>(new FoldOffsets()));
    g->add(std::unique_ptr<Pass>(new ThreadJumps()));
    g->add(std::unique_ptr<Pass>(new DivideByConstant()));
    return g;
}

// **A round is every scalar pass once**; each leaves work for the others,
// so rounds run until one finds nothing, or the level's limit. Labels only
// jumps named are dropped and the flow rebuilt before each.
struct Rounds : Group {
    Rounds() : Group(PassInfo{"rounds", 0, 0, 0, kTodoDropLabels | kTodoBuildFlow}, 0, WhenNoneChanged) {}
};

}

// **The pipeline**, cxx1's passes.def. In order:
//
//   rounds            forward-values, remove-unreachable, remove-dead,
//                     coalesce-copies, fold-loads, fold-offsets, thread-jumps,
//                     divide-by-constant (-O2); repeated
//   frame             (whole, prologue held)
//     webs            every register web a pseudo, its home kept
//     locals          every promotable scalar local a pseudo, its slot kept
//     allocate        every pseudo a register or its slot, the copies coalesced away
//     rounds          (if any was promoted)
//     dse-loop        remove-dead-stores, then rounds; up to three times,
//                     while the stores found something
//   fold-index        an added index register into the memory operand, its scale with it
//   finish-frame      unused saves dropped, the prologue rewritten
//   shrink-loop       shrink, then rounds; up to three times, while shrink
//                     found something; the flow rebuilt before each
//
// The two loops rebuild the flow without dropping labels, where rounds
// does both: that is how the driver did it before the manager, and a
// label dropped there could change what is emitted - kept, and noted.
std::unique_ptr<Pass> pipelineFor() {
    std::unique_ptr<Group> top(new Group(PassInfo{"pipeline", 0, 0, 0, 0}, 1, Group::WhenNoneChanged));
    top->add(rounds<Rounds>());

    std::unique_ptr<Group> frame(new FrameGroup());
    frame->add(std::unique_ptr<Pass>(new Webs()));
    frame->add(std::unique_ptr<Pass>(new LocalsPass()));
    frame->add(std::unique_ptr<Pass>(new Allocate()));
    frame->add(rounds<RoundsIfPromoted>());
    std::unique_ptr<Group> dse(new Group(PassInfo{"dse-loop", 0, 0, 0, 0}, 3, Group::WhenFirstUnchanged));
    dse->add(std::unique_ptr<Pass>(new RemoveDeadStores()));
    dse->add(rounds<Rounds>());
    frame->add(std::move(dse));
    top->add(std::move(frame));

    top->add(std::unique_ptr<Pass>(new FoldIndex()));
    top->add(std::unique_ptr<Pass>(new HoistInvariants()));
    top->add(std::unique_ptr<Pass>(new FinishFrame()));

    std::unique_ptr<Group> shrinkLoop(new Group(PassInfo{"shrink-loop", 0, 0, 0, kTodoBuildFlow}, 3, Group::WhenFirstUnchanged));
    shrinkLoop->add(std::unique_ptr<Pass>(new Shrink()));
    shrinkLoop->add(rounds<Rounds>());
    top->add(std::move(shrinkLoop));
    return std::unique_ptr<Pass>(top.release());
}

}
