// The parser: what is evaluated where it is written.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// **An alias declaration is C++11 and is not a using-declaration** - it names
// a type where the other names an entity, and it is what a program writes in
// place of a typedef.
void Parser::refuseAliasDeclaration() {
    if (!peek().is("using")) return;
    if (peekAt(1).kind != TokenKind::Ident || !peekAt(2).is("=")) return;
    src_.fail(peek().pos, "an alias declaration - 'using X = T;' - is not "
                          "supported yet, though it is C++11: 'typedef T X;' "
                          "says the same thing here");
}

// **`= default` and `= delete` are C++11 and sit exactly where `= 0` does.**
void Parser::refuseDefaultedOrDeleted() {
    if (!peek().is("=")) return;
    const bool def = peekAt(1).is("default");
    if (!def && !peekAt(1).is("delete")) return;
    src_.fail(peek().pos, std::string("'= ") + (def ? "default" : "delete") +
                          "' is not supported yet: a defaulted member is "
                          "written with an empty body here, and a deleted one "
                          "by declaring it private and never defining it");
}

bool Parser::staticAssertion() {
    if (!peek().is("static_assert")) return false;
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");

    const std::size_t condAt = peek().pos;
    ExprPtr cond = decay(conditional());
    long long value;
    if (!fold(*cond, &value, condAt))
        src_.fail(condAt, "the condition of a 'static_assert' has to be a "
                          "constant the compiler can work out here, and this "
                          "is not one");

    if (peek().is(")"))
        src_.fail(peek().pos, "a 'static_assert' with no message is C++17 - "
                              "write the message this one would have printed, "
                              "'static_assert(cond, \"why\")'");
    expect(",");

    if (peek().kind != TokenKind::Str)
        src_.fail(peek().pos, "the message of a 'static_assert' has to be "
                              "written out as a string literal - it is printed "
                              "by the compiler, so there is no program running "
                              "to read a variable");
    std::string message = peek().text;
    at_++;
    while (peek().kind == TokenKind::Str) {   // "a" "b" is one literal
        message += peek().text;
        at_++;
    }

    expect(")");
    expect(";");

    if (value == 0) src_.fail(pos, "static assertion failed: " + message);
    return true;
}

// **The exception specification, and the one thing it does not touch.** In C++11 it is
// *not* part of the function's type - measured, both spellings mangle alike - so what
// it buys is `noexcept(e)`. `throw()` is taken as one; `throw(int)` is refused.
bool Parser::exceptionSpecification() {
    if (consume("noexcept")) {
        if (!consume("(")) return true;
        const std::size_t at = peek().pos;
        const long long value = constantExpression("a constant in 'noexcept('");
        expect(")");
        (void) at;
        return value != 0;
    }
    if (peek().is("throw") && peekAt(1).is("(")) {
        const std::size_t at = peek().pos;
        at_ += 2;
        if (!consume(")"))
            src_.fail(at, "a dynamic exception specification - 'throw(T)' - is "
                          "not supported yet; it needs a run-time check of the "
                          "thrown type against a list, where 'noexcept' is a "
                          "promise the compiler only has to record. 'throw()' "
                          "with nothing in it is 'noexcept' and works");
        return true;
    }
    return false;
}

long long Parser::constantExpression(const char *what) {
    std::size_t pos = peek().pos;
    ExprPtr e = decay(conditional());
    long long v;
    if (!fold(*e, &v, pos))
        src_.fail(pos, std::string("expected ") + what +
                       ", and this is not an integer constant expression");
    return v;
}

const Parser::ConstexprSlot *Parser::constexprSlot(const Var &v) const {
    if (!v.isLocal() || constexprFrames_.empty()) return nullptr;
    const std::vector<ConstexprSlot> &frame = constexprFrames_.back();
    for (std::size_t i = 0; i < frame.size(); i++)
        if (frame[i].offset == v.offset()) return &frame[i];
    return nullptr;
}

bool Parser::enterConstexprCall(const Call &c, const ConstexprFn **out,
                                std::size_t pos) const {
    auto it = constexprFns_.find(c.symbol());
    if (it == constexprFns_.end()) return false;
    const ConstexprFn &fn = it->second;
    if (fn.value == nullptr || c.args().size() != fn.slots.size())
        return false;

    // A recursion that does not end is a compiler that does not either.
    // The standard lets an implementation set a limit and say so; this is
    // that limit, and it is said where it is reached.
    if (constexprFrames_.size() >= 256)
        src_.fail(pos, "this constant expression is more than 256 calls "
                       "deep - a 'constexpr' function that never stops "
                           "recursing cannot be worked out while compiling");

    std::vector<ConstexprSlot> frame;
    for (std::size_t i = 0; i < c.args().size(); i++) {
        ConstexprSlot slot;
        slot.offset = fn.slots[i];
        const Expr &a = *c.args()[i];
        // **Folded outside the new frame, in the caller's.** An argument is an
        // expression where the call is written, so `fact(n - 1)` reads the
        // caller's n; folding it after the push would read the callee's slot.
        slot.floating = a.type() != nullptr && a.type()->isFloating();
        if (slot.floating ? !foldFloating(a, &slot.d) : !fold(a, &slot.i, pos))
            return false;
        frame.push_back(slot);
    }
    constexprFrames_.push_back(frame);
    *out = &fn;
    return true;
}

bool Parser::fold(const Expr &e, long long *out, std::size_t pos) const {
    // **A name, when it names a constant.**
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        // **Inside a constexpr call, a local name is a parameter.** The body being
        // folded belongs to another function, so its Vars name slots in a frame that
        // does not exist; what they are worth is on the top of this stack.
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->floating) return false;   // Cast/Binary fold it as floating
            *out = slot->i;
            return true;
        }
        if (v->isLocal()) {
            if (const Local *l = findLocal(v->name()))
                if (l->isConstantValue) { *out = l->constantValue; return true; }
            return false;
        }
        if (const GlobalSym *g = findGlobal(v->name()))
            if (g->isConstantValue) { *out = g->constantValue; return true; }
        // A static member is not in globals_ - it is kept apart from the
        // member list so nothing walking a layout has to skip it - so the
        // read-back asks the table keyed by its symbol.
        if (const StaticConst *sc = findStaticConst(v->symbol()))
            if (sc->known) { *out = sc->value; return true; }
        return false;
    }
    // **A call to a constexpr function.** C++11 lets its body be one return statement,
    // so running it is folding that expression with the parameters standing for the
    // arguments. A call to anything else simply does not fold, which is the answer.
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (!enterConstexprCall(*c, &fn, pos)) return false;
        long long result = 0;
        const bool ok = fold(*fn->value, &result, pos);
        constexprFrames_.pop_back();
        if (!ok) return false;
        *out = result;
        return true;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) return false;
        *out = n->value();
        return true;
    }

    // **[expr.const] has a floating lane, and this fold answers in integers**,
    // so a floating operand goes to foldDouble where an integer comes out of
    // it - a cast, a comparison, `!` - and a floating result stays its own.
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        long long v;
        if (!e.type()->isInteger()) return false;
        if (c->value().type() != nullptr && c->value().type()->isFloating()) {
            long double d = 0;
            if (!foldFloating(c->value(), &d)) return false;
            v = e.type()->isSigned(target_)
              ? static_cast<long long>(d)
              : static_cast<long long>(static_cast<unsigned long long>(d));
        } else if (!fold(c->value(), &v, pos)) {
            return false;
        }
        *out = narrowTo(v, e.type());
        return true;
    }

    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        long long v;
        if (u->op() == '!' && u->operand().type() != nullptr &&
            u->operand().type()->isFloating()) {
            long double d = 0;
            if (!foldFloating(u->operand(), &d)) return false;
            *out = d == 0;
            return true;
        }
        if (!fold(u->operand(), &v, pos)) return false;
        switch (u->op()) {
        case '-': *out = static_cast<long long>(0ULL - static_cast<unsigned long long>(v)); return true;
        case '+': *out = v; return true;
        case '!': *out = !v; return true;
        case '~': *out = ~v; return true;
        default: return false;
        }
    }

    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        long long t;
        if (!fold(c->cond(), &t, pos)) return false;
        return fold(t ? c->thenArm() : c->elseArm(), out, pos);
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long long l, r;
        const Type *lt = b->lhs().type();
        const Type *rt = b->rhs().type();
        if ((lt != nullptr && lt->isFloating()) ||
            (rt != nullptr && rt->isFloating())) {
            long double dl = 0, dr = 0;
            if (!foldFloating(b->lhs(), &dl) || !foldFloating(b->rhs(), &dr))
                return false;
            switch (b->op()) {
            case BinOp::Eq: *out = (dl == dr); return true;
            case BinOp::Ne: *out = (dl != dr); return true;
            case BinOp::Lt: *out = (dl <  dr); return true;
            case BinOp::Le: *out = (dl <= dr); return true;
            case BinOp::Gt: *out = (dl >  dr); return true;
            case BinOp::Ge: *out = (dl >= dr); return true;
            case BinOp::LAnd: *out = (dl != 0 && dr != 0); return true;
            case BinOp::LOr:  *out = (dl != 0 || dr != 0); return true;
            default: return false;
            }
        }
        if (!fold(b->lhs(), &l, pos) || !fold(b->rhs(), &r, pos)) return false;

        const Type *t = b->lhs().type();
        bool uns = t->isInteger() && !t->isSigned(target_);
        unsigned long long ul = static_cast<unsigned long long>(l);
        unsigned long long ur = static_cast<unsigned long long>(r);

        switch (b->op()) {
        case BinOp::Add: *out = static_cast<long long>(ul + ur); return true;
        case BinOp::Sub: *out = static_cast<long long>(ul - ur); return true;
        case BinOp::Mul: *out = static_cast<long long>(ul * ur); return true;
        case BinOp::Div:
        case BinOp::Mod:
            if (r == 0)
                src_.fail(pos, "division by zero in a constant expression");
            if (!uns && ul == (1ULL << 63) && r == -1) {
                *out = (b->op() == BinOp::Div) ? l : 0;
                return true;
            }
            if (b->op() == BinOp::Div)
                *out = uns ? static_cast<long long>(ul / ur) : l / r;
            else
                *out = uns ? static_cast<long long>(ul % ur) : l % r;
            return true;
        case BinOp::Shl:
        case BinOp::Shr:
            if (r < 0 || r >= 64)
                src_.fail(pos, "shift count out of range in a constant expression");
            if (b->op() == BinOp::Shl) *out = static_cast<long long>(ul << r);
            else *out = uns ? static_cast<long long>(ul >> r) : (l >> r);
            return true;
        case BinOp::BitAnd: *out = l & r; return true;
        case BinOp::BitOr:  *out = l | r; return true;
        case BinOp::BitXor: *out = l ^ r; return true;
        case BinOp::Eq: *out = (l == r); return true;
        case BinOp::Ne: *out = (l != r); return true;
        case BinOp::Lt: *out = uns ? (ul <  ur) : (l <  r); return true;
        case BinOp::Le: *out = uns ? (ul <= ur) : (l <= r); return true;
        case BinOp::Gt: *out = uns ? (ul >  ur) : (l >  r); return true;
        case BinOp::Ge: *out = uns ? (ul >= ur) : (l >= r); return true;
        case BinOp::LAnd: *out = (l && r); return true;
        case BinOp::LOr:  *out = (l || r); return true;
        }
        return false;
    }

    return false;
}

// **[expr.const]/3: a named integral constant is a constant expression**, which is the
// rule that lets C++ write `const int n = 4; int a[n];`. Only integral, because that
// is what fold() answers in. Below it: the one expression a constexpr body may be.
const Expr *Parser::singleReturnValue(const Stmt &body) const {
    const Stmt *at = &body;
    for (;;) {
        const Block *b = dynamic_cast<const Block *>(at);
        if (b == nullptr) break;
        if (b->body().size() != 1) return nullptr;
        at = b->body()[0].get();
    }
    const Return *r = dynamic_cast<const Return *>(at);
    if (r == nullptr || !r->hasValue()) return nullptr;
    return &r->value();
}

bool Parser::constantInitialiser(const Type *t, const Init &in,
                                 long long *out) const {
    if (t == nullptr || !t->isConst() || !t->isInteger()) return false;
    if (in.isList || in.value == nullptr) return false;
    return fold(*in.value, out, in.pos);
}

// The floating twin: a const floating object with a foldable initialiser is
// read back by foldDouble, an integer one being widened on the way in.
bool Parser::constantFloatingInitialiser(const Type *t, const Init &in,
                                         long double *out) const {
    if (t == nullptr || !t->isConst() || !t->isFloating()) return false;
    if (in.isList || in.value == nullptr) return false;
    return foldFloating(*in.value, out);
}

// foldDouble with its two host-precision flags folded away: an answer is an
// answer here, as it is for the integer lane.
bool Parser::foldFloating(const Expr &e, long double *out) const {
    bool past53 = false, x87 = false;
    return foldDouble(e, target_, out, &past53, &x87);
}

long long Parser::narrowTo(long long v, const Type *t) const {
    int bits = t->size(target_) * 8;
    if (bits >= 64) return v;
    unsigned long long mask = (1ULL << bits) - 1;
    unsigned long long kept = static_cast<unsigned long long>(v) & mask;
    if (t->isSigned(target_) && (kept & (1ULL << (bits - 1)))) kept |= ~mask;
    return static_cast<long long>(kept);
}
