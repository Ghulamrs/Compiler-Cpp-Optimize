#pragma once

#include "Type.h"
#include "Mangle.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

class Num;
class Var;
class Assign;
class Unary;
class Binary;
class Call;
class Cast;
class Postfix;
class StrLit;
class VaStart;
class VaArg;
class MemberAccess;
class ExprStmt;
class Return;
class Block;
class If;
class While;
class For;
class DoWhile;
class Switch;
class Case;
class Goto;
class Label;
class Conditional;
class Comma;
class Break;
class Continue;
class FuncletLeave;
class Try;

class Visitor {
public:
    virtual ~Visitor() = default;
    virtual void visit(const Num &) = 0;
    virtual void visit(const Var &) = 0;
    virtual void visit(const Assign &) = 0;
    virtual void visit(const Unary &) = 0;
    virtual void visit(const Binary &) = 0;
    virtual void visit(const Postfix &) = 0;
    virtual void visit(const Call &) = 0;
    virtual void visit(const Cast &) = 0;
    virtual void visit(const StrLit &) = 0;
    virtual void visit(const VaStart &) = 0;
    virtual void visit(const VaArg &) = 0;
    virtual void visit(const MemberAccess &) = 0;
    virtual void visit(const ExprStmt &) = 0;
    virtual void visit(const Return &) = 0;
    virtual void visit(const Block &) = 0;
    virtual void visit(const If &) = 0;
    virtual void visit(const While &) = 0;
    virtual void visit(const For &) = 0;
    virtual void visit(const DoWhile &) = 0;
    virtual void visit(const Switch &) = 0;
    virtual void visit(const Case &) = 0;
    virtual void visit(const Goto &) = 0;
    virtual void visit(const Label &) = 0;
    virtual void visit(const Conditional &) = 0;
    virtual void visit(const Comma &) = 0;
    virtual void visit(const Break &) = 0;
    virtual void visit(const Continue &) = 0;
    virtual void visit(const FuncletLeave &) = 0;
    virtual void visit(const Try &) = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(Visitor &v) const = 0;
};

class Expr : public Node {
public:
    const Type *type() const { return type_; }
    void setType(const Type *t) { type_ = t; }

    // **Set only by `static_cast<T &&>`, and a value category rather than a
    // type**: [basic.lval] separates an lvalue from an xvalue by what may be done
    // with the object, not by how its address is computed. No backend reads it.
    bool isXvalue() const { return xvalue_; }
    void setXvalue() { xvalue_ = true; }
private:
    const Type *type_ = nullptr;
    bool xvalue_ = false;
};

class Stmt : public Node {
public:
    std::size_t pos() const { return pos_; }
    void setPos(std::size_t p) { pos_ = p; }
private:
    std::size_t pos_ = 0;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

enum class BinOp { Add, Sub, Mul, Div, Mod, Shl, Shr,
                   BitAnd, BitOr, BitXor,
                   Eq, Ne, Lt, Le, Gt, Ge, LAnd, LOr };

class Num final : public Expr {
public:
    explicit Num(long long v) : value_(v) {}

    explicit Num(long double d) : dvalue_(d) {}
    long long value() const { return value_; }
    long double dvalue() const { return dvalue_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    long long value_ = 0;
    long double dvalue_ = 0;
};

class Var final : public Expr {
public:
    static Var *local(std::string name, int offset) { return new Var(std::move(name), true, offset); }
    static Var *global(std::string name) { return new Var(std::move(name), false, 0); }

    const std::string &name() const { return name_; }

    // What the linker is told, which is the mangled name once a program has
    // C++ linkage. name() stays what the programmer wrote, because that is
    // what a diagnostic and a debugger should say.
    const std::string &symbol() const { return symbol_.empty() ? name_ : symbol_; }
    void setSymbol(std::string s) { symbol_ = std::move(s); }
    bool isLocal() const { return isLocal_; }
    int offset() const { return offset_; }
    bool readOnly() const { return readOnly_; }
    void setReadOnly(bool r) { readOnly_ = r; }
    bool noAddress() const { return noAddress_; }
    void setNoAddress(bool n) { noAddress_ = n; }
    void accept(Visitor &v) const override { v.visit(*this); }

private:
    Var(std::string name, bool isLocal, int offset)
        : name_(std::move(name)), isLocal_(isLocal), offset_(offset) {}
    std::string name_;
    std::string symbol_;
    bool isLocal_;
    int offset_;
    bool readOnly_ = false;
    bool noAddress_ = false;
};

class StrLit final : public Expr {
public:
    StrLit(std::string label, std::string text)
        : label_(std::move(label)), text_(std::move(text)) {}
    const std::string &label() const { return label_; }
    const std::string &text() const { return text_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string label_;
    std::string text_;
};

class VaStart final : public Expr {
public:
    explicit VaStart(ExprPtr list) : list_(std::move(list)) {}
    const Expr &list() const { return *list_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr list_;
};

class VaArg final : public Expr {
public:
    explicit VaArg(ExprPtr list) : list_(std::move(list)) {}
    const Expr &list() const { return *list_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr list_;
};

class Assign final : public Expr {
public:
    Assign(ExprPtr target, ExprPtr value)
        : target_(std::move(target)), value_(std::move(value)) {}
    const Expr &target() const { return *target_; }
    const Expr &value() const { return *value_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr target_, value_;
};

class Unary final : public Expr {
public:
    Unary(char op, ExprPtr operand) : op_(op), operand_(std::move(operand)) {}
    char op() const { return op_; }
    const Expr &operand() const { return *operand_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    char op_;
    ExprPtr operand_;
};

class Binary final : public Expr {
public:
    Binary(BinOp op, ExprPtr lhs, ExprPtr rhs)
        : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}
    BinOp op() const { return op_; }
    const Expr &lhs() const { return *lhs_; }
    const Expr &rhs() const { return *rhs_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    BinOp op_;
    ExprPtr lhs_, rhs_;
};

class Call final : public Expr {
public:
    Call(std::string name, ExprPtr callee, std::vector<ExprPtr> args, bool variadic,
         int resultSlot = 0, int namedArgs = -1, std::vector<int> argSlots = {})
        : name_(std::move(name)), callee_(std::move(callee)),
          args_(std::move(args)), variadic_(variadic), resultSlot_(resultSlot),
          namedArgs_(namedArgs < 0 ? static_cast<int>(args_.size()) : namedArgs),
          argSlots_(std::move(argSlots)) {}
    const std::string &name() const { return name_; }
    const std::string &symbol() const { return symbol_.empty() ? name_ : symbol_; }
    void setSymbol(std::string s) { symbol_ = std::move(s); }
    const std::vector<ExprPtr> &args() const { return args_; }
    const Expr *callee() const { return callee_.get(); }
    bool isVariadic() const { return variadic_; }
    int resultSlot() const { return resultSlot_; }
    // Copy elision points this at the variable being initialised, so the callee builds its result there and no copy constructor runs.
    void setResultSlot(int s) { resultSlot_ = s; }

    int namedArgs() const { return namedArgs_; }

    int argSlot(std::size_t i) const {
        return i < argSlots_.size() ? argSlots_[i] : 0;
    }

    // **Whether the first argument is a `this` pointer**, which the Microsoft ABI
    // needs and the Itanium one does not: cl puts `this` first and the hidden
    // return pointer second. A lowered member call needs this to be told apart.
    bool hasThis() const { return hasThis_; }
    void setHasThis(bool t) { hasThis_ = t; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string name_;
    std::string symbol_;
    ExprPtr callee_;
    std::vector<ExprPtr> args_;
    bool variadic_;
    int resultSlot_;
    int namedArgs_;
    std::vector<int> argSlots_;
    bool hasThis_ = false;
};

class Postfix final : public Expr {
public:
    Postfix(ExprPtr target, bool increment, long long step)
        : target_(std::move(target)), increment_(increment), step_(step) {}
    const Expr &target() const { return *target_; }
    bool increment() const { return increment_; }
    long long step() const { return step_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr target_;
    bool increment_;
    long long step_;
};

class Cast final : public Expr {
public:
    Cast(const Type *to, ExprPtr value) : value_(std::move(value)) { setType(to); }
    const Expr &value() const { return *value_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr value_;
};

class Comma final : public Expr {
public:
    Comma(ExprPtr left, ExprPtr right)
        : left_(std::move(left)), right_(std::move(right)) {}
    const Expr &left() const { return *left_; }
    const Expr &right() const { return *right_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr left_, right_;
};

class Conditional final : public Expr {
public:
    Conditional(ExprPtr cond, ExprPtr thenArm, ExprPtr elseArm)
        : cond_(std::move(cond)), then_(std::move(thenArm)),
          else_(std::move(elseArm)) {}
    const Expr &cond() const { return *cond_; }
    const Expr &thenArm() const { return *then_; }
    const Expr &elseArm() const { return *else_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_, then_, else_;
};

class MemberAccess final : public Expr {
public:
    MemberAccess(ExprPtr object, std::string name, int offset,
                 int width = 0, int bitOffset = 0)
        : object_(std::move(object)), name_(std::move(name)), offset_(offset),
          width_(width), bitOffset_(bitOffset) {}
    const Expr &object() const { return *object_; }
    const std::string &name() const { return name_; }
    int offset() const { return offset_; }
    int width() const { return width_; }
    int bitOffset() const { return bitOffset_; }
    bool isBitField() const { return width_ != 0; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr object_;
    std::string name_;
    int offset_;
    int width_;
    int bitOffset_;
};

class ExprStmt final : public Stmt {
public:
    explicit ExprStmt(ExprPtr e) : expr_(std::move(e)) {}
    const Expr &expr() const { return *expr_; }
    // **Handing the expression back out.**
    ExprPtr release() { return std::move(expr_); }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr expr_;
};

class Return final : public Stmt {
public:
    explicit Return(ExprPtr value) : value_(std::move(value)) {}
    bool hasValue() const { return value_ != nullptr; }
    const Expr &value() const { return *value_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr value_;
};

class Block final : public Stmt {
public:
    explicit Block(std::vector<StmtPtr> body) : body_(std::move(body)) {}
    const std::vector<StmtPtr> &body() const { return body_; }
    // For the one block that is filled in after it is placed.
    void append(StmtPtr s) { body_.push_back(std::move(s)); }

    int scope() const { return scope_; }
    void setScope(int s) { scope_ = s; }
    // **Run while an exception unwinds** - a pad's destructors and the
    // resume - so a throw out of it is [except.terminate]'s, not a new one.
    void setUnwindCleanup() { unwindCleanup_ = true; }
    bool unwindCleanup() const { return unwindCleanup_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::vector<StmtPtr> body_;
    int scope_ = -1;
    bool unwindCleanup_ = false;
};

class If final : public Stmt {
public:
    If(ExprPtr cond, StmtPtr thenArm, StmtPtr elseArm)
        : cond_(std::move(cond)), then_(std::move(thenArm)), else_(std::move(elseArm)) {}
    const Expr &cond() const { return *cond_; }
    const Stmt &thenArm() const { return *then_; }
    const Stmt *elseArm() const { return else_.get(); }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_;
    StmtPtr then_, else_;
};

class While final : public Stmt {
public:
    While(ExprPtr cond, StmtPtr body)
        : cond_(std::move(cond)), body_(std::move(body)) {}
    const Expr &cond() const { return *cond_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_;
    StmtPtr body_;
};

class For final : public Stmt {
public:
    For(StmtPtr init, ExprPtr cond, ExprPtr step, StmtPtr body)
        : init_(std::move(init)), cond_(std::move(cond)),
          step_(std::move(step)), body_(std::move(body)) {}
    const Stmt *init() const { return init_.get(); }
    const Expr *cond() const { return cond_.get(); }
    const Expr *step() const { return step_.get(); }
    const Stmt &body() const { return *body_; }

    int scope() const { return scope_; }
    void setScope(int s) { scope_ = s; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    StmtPtr init_;
    ExprPtr cond_, step_;
    StmtPtr body_;
    int scope_ = -1;
};

class DoWhile final : public Stmt {
public:
    DoWhile(StmtPtr body, ExprPtr cond)
        : body_(std::move(body)), cond_(std::move(cond)) {}
    const Stmt &body() const { return *body_; }
    const Expr &cond() const { return *cond_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    StmtPtr body_;
    ExprPtr cond_;
};

class Case final : public Stmt {
public:
    Case(long long value, bool isDefault, int id, StmtPtr body)
        : value_(value), isDefault_(isDefault), id_(id), body_(std::move(body)) {}
    long long value() const { return value_; }
    bool isDefault() const { return isDefault_; }
    int id() const { return id_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    long long value_;
    bool isDefault_;
    int id_;
    StmtPtr body_;
};

class Switch final : public Stmt {
public:
    Switch(ExprPtr cond, StmtPtr body, std::vector<const Case *> cases,
           const Case *deflt)
        : cond_(std::move(cond)), body_(std::move(body)),
          cases_(std::move(cases)), default_(deflt) {}
    const Expr &cond() const { return *cond_; }
    const Stmt &body() const { return *body_; }
    const std::vector<const Case *> &cases() const { return cases_; }
    const Case *defaultCase() const { return default_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    ExprPtr cond_;
    StmtPtr body_;
    std::vector<const Case *> cases_;
    const Case *default_;
};

class Goto final : public Stmt {
public:
    explicit Goto(std::string label) : label_(std::move(label)) {}
    const std::string &label() const { return label_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string label_;
};

class Label final : public Stmt {
public:
    Label(std::string name, StmtPtr body)
        : name_(std::move(name)), body_(std::move(body)) {}
    const std::string &name() const { return name_; }
    const Stmt &body() const { return *body_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string name_;
    StmtPtr body_;
};

class Break final : public Stmt {
public:
    void accept(Visitor &v) const override { v.visit(*this); }
};

class Continue final : public Stmt {
public:
    void accept(Visitor &v) const override { v.visit(*this); }
};

// **Leaving a Microsoft handler funclet early**: the runtime carries on at the
// label named, which the parser laid down in the parent after the `try`.
class FuncletLeave final : public Stmt {
public:
    explicit FuncletLeave(std::string label) : label_(std::move(label)) {}
    const std::string &label() const { return label_; }
    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::string label_;
};

struct Param {
    const Type *type;
    int offset;
};

// `try { ... } catch (T e) { ... }` - rung 6.3, and almost all of it is ordinary
// statements. Itanium wants the labels bounding the body, the pad, and the
// type_info symbols in order; Microsoft keeps each handler whole, to be called.
struct MsHandler {
    std::string descriptor;    // ??_R0H@8 and the like; empty for catch (...)
    int objectSlot = 0;        // frame slot for the caught object, 0 if unnamed
    int objectSize = 0;
    bool byReference = false;  // the slot takes the runtime's pointer, not a copy
    bool constPointer = false; // `catch (const T *)`: HT_IsConst, the const off the name
    StmtPtr body;
};

class Try final : public Stmt {
public:
    // The body is a *list* and not a block, because a cleanup region covers a
    // slice of an enclosing block's statements and must not open a scope of its
    // own - the objects it destroys belong to the block outside it.
    Try(std::vector<StmtPtr> body, StmtPtr pad, int pointerSlot,
        int selectorSlot, std::vector<std::string> types)
        : body_(std::move(body)), pad_(std::move(pad)),
          pointerSlot_(pointerSlot), selectorSlot_(selectorSlot),
          types_(std::move(types)) {}
    const std::vector<StmtPtr> &body() const { return body_; }

    // **Null on Windows.**
    bool hasPad() const { return pad_ != nullptr; }
    // **A `try` whose pad also unwinds this frame's objects.** Its call sites
    // then need a trailing cleanup action, or phase 2 installs no pad where
    // no handler matched and the destructors never run.
    void setAlsoCleanup() { alsoCleanup_ = true; }
    bool alsoCleanup() const { return alsoCleanup_; }
    const Stmt &pad() const { return *pad_; }
    int pointerSlot() const { return pointerSlot_; }
    int selectorSlot() const { return selectorSlot_; }
    const std::vector<std::string> &types() const { return types_; }
    // **Set late, and only for a `try` body's cleanup rows.**
    void setTypes(std::vector<std::string> t) { types_ = std::move(t); }

    // **The selector index of each of those types, as the parser numbered
    // them.** It writes `if (sel == n)` into the handler chain, so it - and
    // not the backend - is where n comes from.
    const std::vector<int> &typeIndices() const { return typeIndices_; }
    void setTypeIndices(std::vector<int> ix) { typeIndices_ = std::move(ix); }

    const std::vector<MsHandler> &handlers() const { return handlers_; }
    void setHandlers(std::vector<MsHandler> h) { handlers_ = std::move(h); }

    // **A cleanup region, where the Microsoft ABI wants a state rather than a
    // handler.** Nothing is caught here, and it holds what *this* region built
    // and no more: the runtime chains to the region before it.
    const Stmt *cleanup() const { return cleanup_.get(); }
    void setCleanup(StmtPtr c) { cleanup_ = std::move(c); }

    // Where the runtime's four-word scratch area sits in this frame. Written
    // as -2 on entry by the parent and read by the personality routine
    // through the FuncInfo's dispUnwindHelp.
    int unwindHelpSlot() const { return unwindHelpSlot_; }
    void setUnwindHelpSlot(int s) { unwindHelpSlot_ = s; }
    // The labels a handler funclet may hand back as where to carry on, so the
    // flow knows the body's calls can reach them.
    const std::vector<std::string> &msExits() const { return msExits_; }
    void setMsExits(std::vector<std::string> e) { msExits_ = std::move(e); }

    void accept(Visitor &v) const override { v.visit(*this); }
private:
    std::vector<StmtPtr> body_;
    StmtPtr pad_;
    int pointerSlot_;
    int selectorSlot_;
    bool alsoCleanup_ = false;
    std::vector<std::string> msExits_;
    std::vector<std::string> types_;
    std::vector<int> typeIndices_;
    std::vector<MsHandler> handlers_;
    StmtPtr cleanup_;
    int unwindHelpSlot_ = 0;
};

struct Local {
    std::string name;
    const Type *type;
    int offset;
    bool isParam;

    std::string staticName;

    int scope = 0;
};

class Function {
public:
    Function(std::string name, const Type *returns, std::vector<Param> params,
             StmtPtr body, int frameSize, bool isStatic, int sretSlot = 0,
             bool variadic = false, int regSaveSlot = 0, std::size_t pos = 0,
             std::vector<Local> locals = std::vector<Local>())
        : name_(std::move(name)), returns_(returns), params_(std::move(params)),
          body_(std::move(body)), frameSize_(frameSize), isStatic_(isStatic),
          sretSlot_(sretSlot), variadic_(variadic), regSaveSlot_(regSaveSlot),
          pos_(pos), locals_(std::move(locals)) {}
    const std::string &name() const { return name_; }
    const std::string &symbol() const { return symbol_.empty() ? name_ : symbol_; }
    void setSymbol(std::string s) { symbol_ = std::move(s); }

    // The definition side of Call::hasThis - the same question asked of the
    // function being emitted rather than of a call to one.
    bool hasThis() const { return hasThis_; }
    void setHasThis(bool t) { hasThis_ = t; }

    // **Implicitly inline** - [dcl.inline]/6 - so the definition may appear in
    // several translation units and the linker has to fold the copies rather
    // than reject them.
    bool isInline() const { return isInline_; }
    void setInline(bool v) { isInline_ = v; }

    // A second name for the same code, emitted as an extra label in front of it.
    // Itanium gives a constructor C1 for a complete object and C2 for a base and
    // clang emits both; empty for everything else, Microsoft included.
    const std::string &alias() const { return alias_; }
    void setAlias(std::string a) { alias_ = std::move(a); }
    const Type *returns() const { return returns_; }
    const std::vector<Param> &params() const { return params_; }
    const Stmt &body() const { return *body_; }
    int frameSize() const { return frameSize_; }
    bool isStatic() const { return isStatic_; }
    int sretSlot() const { return sretSlot_; }
    bool isVariadic() const { return variadic_; }

    int regSaveSlot() const { return regSaveSlot_; }

    std::size_t pos() const { return pos_; }
    const std::vector<Local> &locals() const { return locals_; }

    const std::vector<int> &blocks() const { return blocks_; }
    void setBlocks(std::vector<int> b) { blocks_ = std::move(b); }

    // Whether this function has a landing pad in it, which the prologue has
    // to know before the body is walked: the personality routine and the LSDA
    // are named there.
    bool hasLandingPads() const { return landingPads_; }
    void setHasLandingPads(bool b) { landingPads_ = b; }
    // Declared noexcept (or throw()): a table that says so lets the runtime end the program itself.
    bool isNoexcept() const { return noexcept_; }
    void setNoexcept(bool b) { noexcept_ = b; }
private:
    bool noexcept_ = false;
    bool hasThis_ = false;
    bool isInline_ = false;
    std::string name_;
    std::string symbol_;
    std::string alias_;
    const Type *returns_;
    std::vector<Param> params_;
    StmtPtr body_;
    int frameSize_;
    bool isStatic_;
    int sretSlot_;
    bool variadic_;
    int regSaveSlot_;
    std::size_t pos_;
    std::vector<Local> locals_;
    std::vector<int> blocks_;
    bool landingPads_ = false;
};

struct GlobalPiece {
    int offset;
    int size;
    long long value;

    std::string symbol;
};

struct Global {
    std::string name;

    // The linker's name for it, which differs from 'name' only where the
    // platform ABI mangles a variable - Microsoft does, Itanium does not.
    std::string symbol;
    const Type *type;
    std::vector<GlobalPiece> init;
    bool hasInit;
    bool isStatic;

    bool isConst;

    // **One 8-byte pointer laid down immediately before this object's label.**
    std::string prefixWord = std::string();

    // **Folded rather than rejected**, for an object several translation units each define: a
    // vtable, a typeinfo and its name string. clang marks all three weak, and without it two units
    // that share a polymorphic class collide on every one of them.
    bool isInline = false;
    int align = 0;   // `alignas(N)` on the object: what it asked for beyond its type's, else 0
};

struct StringLit {
    std::string label;
    std::string bytes;
    int width;
};

struct Program {
    std::vector<Function> functions;
    std::vector<Global> globals;
    std::vector<StringLit> strings;
    // **The types this file throws**, which only the Microsoft backend reads.
    std::vector<const Type *> thrown;
    // **The Microsoft ThrowInfo chains**, one per type thrown or caught, finished by the parser.
    std::vector<MicrosoftThrow> msThrows;
    // **The classes wanting a Microsoft run-time description**, five each.
    std::vector<const Type *> rtti;
    // **The function that runs before main** - [basic.start.init]/2.
    std::string initFunction;
    // Whether it names __dso_handle, which ELF wants declared hidden.
    bool usesDsoHandle = false;
    // **Whether the file wrote `volatile` anywhere.** The type system drops it,
    // so the optimizer cannot tell a volatile read from a plain one: such a
    // file is compiled without -O (see Driver::compile).
    bool usesVolatile = false;
};
