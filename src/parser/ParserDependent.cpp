// Expression SFINAE: an expression over a template's parameters, read as a tree in a pattern.
// docs/mend-1.14/expression-sfinae-design.md is the design, every production measured with clang.

#include "Parser.h"
#include "../Source.h"

#include <cstring>

// **Whether the operand ahead mentions a parameter of the pattern being read**, scanning to the
// first `,` `>` `>>` (with `angle`) or unbalanced `)` at depth 0: clang's "instantiation-dependent".
bool Parser::dependentAhead(bool angle) const {
    // A template-id's own `<...>` is inside the operand: `is_base_of<C, T>::value` is one argument, not two.
    int ids = 0;
    for (std::size_t k = 0, depth = 0; peekAt(k).kind != TokenKind::End; k++) {
        const Token &t = peekAt(k);
        if (t.is("<") && k > 0 && peekAt(k - 1).kind == TokenKind::Ident && isTemplateName(peekAt(k - 1).text)) {
            ids++;
            continue;
        }
        if (ids > 0 && (t.is(">") || t.is(">>"))) {
            ids -= t.is(">>") ? 2 : 1;
            if (ids < 0) { if (angle && depth == 0) return false; ids = 0; }
            continue;
        }
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) {
            if (depth == 0) return false;
            depth--;
        } else if (depth == 0 && ids == 0 && (t.is(";") || (angle && (t.is(",") || t.is(">") || t.is(">>")))))
            return false;
        else if (t.kind == TokenKind::Ident) {
            if (k > 0 && (peekAt(k - 1).is(".") || peekAt(k - 1).is("->"))) continue;
            if (nonTypePatternParams_.count(t.text) != 0) return true;
            // A pack bound to its members in an instantiation depends on nothing; one in a pattern does.
            const auto pk = packs_.find(t.text);
            if (pk != packs_.end()) {
                for (const Type *m : pk->second.types)
                    if (dependentType(m)) return true;
            }
            if (boundTypeParams_.count(t.text) != 0) {
                const Type *bound = findTypedef(t.text);
                if (bound != nullptr && dependentType(bound)) return true;
            }
            if (const Local *l = findLocal(t.text))
                if (l->trailingParam >= 0 && dependentType(l->type)) return true;
        }
    }
    return false;
}

const DepExpr *Parser::depNode(DepExpr e) { return types_.intern(std::move(e)); }

void Parser::depRefuse(std::size_t pos, const std::string &what) {
    src_.fail(pos, what + " is not supported yet in an expression over a template's parameters in "
                          "a signature - the Itanium name would have to spell it, and that form was "
                          "not measured");
}

// expression := assignment [, assignment]... - the comma only where `comma` lets it in.
const DepExpr *Parser::dependentExpression(bool angle, bool comma) {
    const DepExpr *e = depAssignment(angle);
    while (comma && peek().is(",")) {
        at_++;
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = "cm";
        n.kids = { e, depAssignment(angle) };
        e = depNode(n);
    }
    return e;
}

const DepExpr *Parser::depAssignment(bool angle) {
    const DepExpr *lhs = depBinary(1, angle);
    if (peek().is("?")) {
        at_++;
        const DepExpr *a = dependentExpression(false, true);
        expect(":");
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = "qu";
        n.kids = { lhs, a, depAssignment(angle) };
        return depNode(n);
    }
    if (peek().is("=")) {
        at_++;
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = "aS";
        n.kids = { lhs, depAssignment(angle) };
        return depNode(n);
    }
    static const char *const compound[] = { "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
                                            "<<=", ">>=" };
    for (const char *c : compound)
        if (peek().is(c)) depRefuse(peek().pos, "a compound assignment '" + std::string(c) + "'");
    return lhs;
}

// Left-associative, the tree clang builds: `sizeof(T) - 1 - 2` is `mimistT_Li1ELi2E`.
const DepExpr *Parser::depBinary(int least, bool angle) {
    struct Op { const char *token; const char *code; int prec; };
    static const Op ops[] = {
        { "||", "oo", 1 }, { "&&", "aa", 2 }, { "|", "or", 3 }, { "^", "eo", 4 },
        { "&", "an", 5 }, { "==", "eq", 6 }, { "!=", "ne", 6 }, { "<", "lt", 7 },
        { ">", "gt", 7 }, { "<=", "le", 7 }, { ">=", "ge", 7 }, { "<<", "ls", 8 },
        { ">>", "rs", 8 }, { "+", "pl", 9 }, { "-", "mi", 9 }, { "*", "ml", 10 },
        { "/", "dv", 10 }, { "%", "rm", 10 },
    };
    const DepExpr *lhs = depUnary(angle);
    for (;;) {
        const Op *found = nullptr;
        for (const Op &o : ops)
            if (peek().is(o.token)) { found = &o; break; }
        if (found == nullptr || found->prec < least) return lhs;
        if (angle && (std::strcmp(found->token, ">") == 0 || std::strcmp(found->token, ">>") == 0))
            return lhs;                           // the argument list's own `>`
        at_++;
        DepExpr n;
        n.op = DepExpr::Operator;
        n.code = found->code;
        n.kids = { lhs, depBinary(found->prec + 1, angle) };
        lhs = depNode(n);
    }
}

const DepExpr *Parser::depUnary(bool angle) {
    const std::size_t pos = peek().pos;
    static const char *const prefix[][2] = { { "-", "ng" }, { "!", "nt" }, { "~", "co" },
                                             { "&", "ad" }, { "*", "de" }, { "+", "ps" },
                                             { "++", "pp" }, { "--", "mm" } };
    for (const auto &p : prefix)
        if (peek().is(p[0])) {
            at_++;
            DepExpr n;
            n.op = DepExpr::Operator;
            n.code = p[1];
            n.kids = { depUnary(angle) };
            return depNode(n);
        }
    if (peek().is("sizeof")) {
        at_++;
        DepExpr n;
        // `sizeof...(I)` of a template parameter pack: `Table<int, sizeof...(I)> squares(Seq<I...>)` (1.82: refused).
        if (peek().is("...")) {
            at_++;
            const bool paren = consume("(");
            const auto pk = peek().kind == TokenKind::Ident ? packs_.find(peek().text) : packs_.end();
            if (pk == packs_.end() || pk->second.functionParameter || pk->second.types.size() != 1 ||
                pk->second.types[0] == nullptr || pk->second.types[0]->kind() != Kind::TemplateParam)
                depRefuse(pos, "'sizeof...' of anything but a template parameter pack");
            n.op = DepExpr::SizeofPack;
            n.index = static_cast<int>(pk->second.types[0]->length());
            n.type = types_.get(target_.sizeType());
            at_++;
            if (paren) expect(")");
            return depNode(n);
        }
        if (peek().is("(") && parenHoldsTypeId()) {
            at_++;
            StorageClass sc;
            const Type *t = declarator(specifiers(&sc), true).type;
            expect(")");
            // [mangle]: an operand that depends on nothing is spelled as the size it has.
            if (!dependentType(t)) {
                if (!t->isComplete()) src_.fail(pos, "sizeof needs a complete type");
                n.op = DepExpr::Literal;
                n.type = types_.get(target_.sizeType());
                n.value = t->size(target_);
                return depNode(n);
            }
            n.op = DepExpr::SizeofType;
            n.type = t;
            return depNode(n);
        }
        n.op = DepExpr::SizeofExpr;
        n.kids = { depUnary(angle) };
        return depNode(n);
    }
    if (peek().is("alignof") || peek().is("noexcept") || peek().is("new") ||
        peek().is("delete") || peek().is("throw"))
        depRefuse(pos, "'" + peek().text + "'");
    // A C-style cast is spelled as a one-argument functional cast: `(int)t` is `cvifp_`.
    if (peek().is("(") && parenHoldsTypeId()) {
        at_++;
        StorageClass sc;
        const Type *t = declarator(specifiers(&sc), true).type;
        expect(")");
        DepExpr n;
        n.op = DepExpr::Cast;
        n.type = t;
        n.kids = { depUnary(angle) };
        return depNode(n);
    }
    return depPostfix(angle);
}

const DepExpr *Parser::depPostfix(bool angle) {
    const DepExpr *e = depPrimary(angle);
    for (;;) {
        const std::size_t pos = peek().pos;
        if (peek().is(".") || peek().is("->")) {
            const bool arrow = peek().is("->");
            at_++;
            if (peek().kind != TokenKind::Ident)
                depRefuse(pos, "a member named by '" + peek().text + "'");
            if (peekAt(1).is("::")) depRefuse(pos, "a qualified member name");
            if (peekAt(1).is("<")) depRefuse(pos, "a member template named with arguments");
            DepExpr n;
            n.op = arrow ? DepExpr::Arrow : DepExpr::Member;
            n.name = peek().text;
            n.kids = { e };
            at_++;
            e = depNode(n);
        } else if (peek().is("(")) {
            at_++;
            DepExpr n;
            n.op = DepExpr::Call;
            n.kids = { e };
            while (!consume(")")) {
                n.kids.push_back(dependentExpression(false, false));
                // `std::forward<A>(a)...`, [temp.variadic]/4: the argument repeated for each member.
                if (consume("...")) {
                    DepExpr x;
                    x.op = DepExpr::PackExpansion;
                    x.kids = { n.kids.back() };
                    n.kids.back() = depNode(x);
                }
                if (!peek().is(")")) expect(",");
            }
            e = depNode(n);
        } else if (peek().is("[")) {
            at_++;
            DepExpr n;
            n.op = DepExpr::Operator;
            n.code = "ix";
            n.kids = { e, dependentExpression(false, true) };
            expect("]");
            e = depNode(n);
        } else if (peek().is("++") || peek().is("--")) {
            depRefuse(pos, "a postfix '" + peek().text + "'");
        } else if (peek().is(".*") || peek().is("->*")) {
            depRefuse(pos, "'" + peek().text + "'");
        } else {
            return e;
        }
    }
}

// `T(x)` and `T(1, 2)` and `T()`: `cv <type> <e>` for one argument, `cv <type> _ <e>... E` else.
const DepExpr *Parser::depCast(const Type *to) {
    const std::size_t pos = peek().pos;
    if (peek().is("{")) depRefuse(pos, "a braced functional cast");
    expect("(");
    DepExpr n;
    n.type = to;
    while (!consume(")")) {
        n.kids.push_back(dependentExpression(false, false));
        if (!peek().is(")")) expect(",");
    }
    n.op = n.kids.size() == 1 ? DepExpr::Cast : DepExpr::CastList;
    return depNode(n);
}

const DepExpr *Parser::depPrimary(bool angle) {
    (void)angle;
    const Token &tok = peek();
    const std::size_t pos = tok.pos;
    DepExpr n;
    if (tok.kind == TokenKind::Num || tok.is("true") || tok.is("false")) {
        Discarded held(this);
        Unevaluated unevaluated(this);
        ExprPtr lit = primary(current_);
        const Num *num = dynamic_cast<const Num *>(lit.get());
        if (num == nullptr || lit->type() == nullptr) depRefuse(pos, "this literal");
        n.op = DepExpr::Literal;
        n.type = lit->type();
        if (n.type->kind() == Kind::Float || n.type->kind() == Kind::Double) {
            n.floating = true;
            if (n.type->kind() == Kind::Float) {
                const float f = static_cast<float>(num->dvalue());
                unsigned int bits;
                std::memcpy(&bits, &f, sizeof bits);
                n.value = static_cast<long long>(bits);
            } else {
                const double d = static_cast<double>(num->dvalue());
                unsigned long long bits;
                std::memcpy(&bits, &d, sizeof bits);
                n.value = static_cast<long long>(bits);
            }
        } else if (n.type->isFloating()) {
            depRefuse(pos, "a 'long double' literal");
        } else {
            n.value = num->value();
        }
        return depNode(n);
    }
    if (tok.kind == TokenKind::Str) depRefuse(pos, "a string literal");
    if (tok.is("(")) {
        at_++;
        const DepExpr *e = dependentExpression(false, true);
        expect(")");
        return e;
    }
    if (tok.is("static_cast")) {
        at_++;
        expect("<");
        const bool wasArgs = inTemplateArgs_;
        inTemplateArgs_ = true;
        StorageClass sc;
        const Type *t = declarator(specifiers(&sc), true).type;
        inTemplateArgs_ = wasArgs;
        takeClosingAngle();
        expect("(");
        n.op = DepExpr::StaticCast;
        n.type = t;
        n.kids = { dependentExpression(false, true) };
        expect(")");
        return depNode(n);
    }
    if (tok.is("this") || tok.is("dynamic_cast") || tok.is("reinterpret_cast") ||
        tok.is("const_cast") || tok.is("typeid") || tok.is("[") || tok.is("::") ||
        tok.is("nullptr") || tok.is("operator"))
        depRefuse(pos, "'" + tok.text + "'");
    if (tok.is("typename")) {
        StorageClass sc;
        return depCast(specifiers(&sc));
    }
    if (tok.kind == TokenKind::Ident) {
        const std::string &name = tok.text;
        // **`std::is_integral<T>::value`, `Mine<T>::value`** - a static member of a class template's
        // specialization over the parameters: the dependent member the type reader makes, read as a value.
        {
            std::size_t k = 0;
            std::string written = peekAt(0).text;
            while (peekAt(k + 1).is("::") && peekAt(k + 2).kind == TokenKind::Ident) {
                k += 2;
                written += "::" + peekAt(k).text;
            }
            if (peekAt(k + 1).is("<") && isClassTemplate(written)) {
                std::size_t j = k + 1;
                for (int depth = 0; peekAt(j).kind != TokenKind::End; j++) {
                    if (depth > 0 && (peekAt(j).is("(") || peekAt(j).is("["))) { j = groupCloseAt(at_ + j) - at_; continue; }
                    if (peekAt(j).is("<")) depth++;
                    else if (peekAt(j).is(">")) { if (--depth == 0) break; }
                    else if (peekAt(j).is(">>")) { depth -= 2; if (depth <= 0) break; }
                }
                if (peekAt(j + 1).is("::") && peekAt(j + 2).kind == TokenKind::Ident && !peekAt(j + 3).is("(") &&
                    !peekAt(j + 3).is("<") && !peekAt(j + 3).is("::")) {
                    // Arguments naming only parameters already bound to types - `R<T>::value` in a member
                    // template of complex<double> - make a real class, whose member is a constant.
                    bool concrete = true;
                    for (std::size_t m = k + 2; m < j && concrete; m++) {
                        const Token &a = peekAt(m);
                        if (a.kind != TokenKind::Ident) continue;
                        if (nonTypePatternParams_.count(a.text) != 0 || packs_.count(a.text) != 0) concrete = false;
                        else if (const Type *bt = findTypedef(a.text)) concrete = !dependentType(bt);
                    }
                    if (concrete) {
                        struct Pattern {
                            bool &f; bool was;
                            ~Pattern() { f = was; }
                        } pattern{ patternOnly_, patternOnly_ };
                        patternOnly_ = false;
                        Discarded held(this);
                        Unevaluated unevaluated(this);
                        ExprPtr v = primary(current_);
                        long long value = 0;
                        if (v->type() == nullptr || !v->type()->isInteger() || !fold(*v, &value, pos))
                            depRefuse(pos, "the member '" + peekAt(0).text + "' of a class template's specialization");
                        n.op = DepExpr::Literal;
                        n.type = v->type()->unqualified();
                        n.value = value;
                        return depNode(n);
                    }
                    const bool wasPattern = patternOnly_;
                    patternOnly_ = true;
                    StorageClass sc;
                    const Type *t = specifiers(&sc);
                    patternOnly_ = wasPattern;
                    if (t->kind() == Kind::DependentMember) {
                        n.op = DepExpr::ScopedMember;
                        n.type = t;
                        return depNode(n);
                    }
                    depRefuse(pos, "the member '" + peekAt(0).text + "' of a class template's specialization");
                }
            }
        }
        if (isTypeTraitName(name) && peekAt(1).is("(")) {
            n.op = DepExpr::TypeTrait;
            n.name = name;
            at_ += 2;
            while (!consume(")")) {
                // `Args...`: the pack standing for itself, `DpT0_`.
                auto pk = peek().kind == TokenKind::Ident && peekAt(1).is("...") ? packs_.find(peek().text)
                                                                                  : packs_.end();
                const Type *t = nullptr;
                if (pk != packs_.end() && pk->second.standsForItself()) {
                    t = types_.packExpansion(pk->second.types[0]);
                    at_ += 2;
                } else {
                    StorageClass sc;
                    t = declarator(specifiers(&sc), true).type;
                    if (consume("...")) t = types_.packExpansion(t);
                }
                n.targs.push_back(t);
                if (!peek().is(")")) expect(",");
            }
            return depNode(n);
        }
        // A function parameter pack in the pattern is its parameter, `fp0_`; a pack of an instantiation read
        // before may still be listed under the same name.
        const Local *asParam = findLocal(name);
        if (packs_.count(name) != 0 && (asParam == nullptr || asParam->trailingParam < 0))
            depRefuse(pos, "a parameter pack");
        // **`N::K::get(t)`, `std::declval<T>()`: a function named qualified**, whose name depends on nothing.
        // The one function it names is spelled by its symbol; an overload set or a template by its name.
        std::size_t chain = 1;
        while (peekAt(chain).is("::") && peekAt(chain + 1).kind == TokenKind::Ident) chain += 2;
        if (chain > 1 && boundTypeParams_.count(name) == 0 && qualifiedTypeEnd() != chain) {
            std::vector<std::string> scope;
            for (std::size_t k = 0; k + 1 < chain; k += 2) scope.push_back(peekAt(k).text);
            const std::string leaf = peekAt(chain - 1).text;
            std::string full;
            for (const std::string &s : scope) full += s + "::";
            full += leaf;
            at_ += chain;
            std::vector<const Type *> targs;
            if (peek().is("<")) {
                at_++;
                const bool wasArgs = inTemplateArgs_;
                inTemplateArgs_ = true;
                while (!peek().is(">") && !peek().is(">>")) {
                    // A pack named inside the pattern of an expansion is its member, `T0_`; `A...` all of them.
                    const auto pk = peek().kind == TokenKind::Ident ? packs_.find(peek().text) : packs_.end();
                    if (pk != packs_.end() && pk->second.standsForItself() &&
                        (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>") || peekAt(1).is("..."))) {
                        at_++;
                        targs.push_back(consume("...") ? types_.packExpansion(pk->second.types[0])
                                                       : pk->second.types[0]);
                        if (!consume(",")) break;
                        continue;
                    }
                    StorageClass sc;
                    targs.push_back(declarator(specifiers(&sc), true).type);
                    if (!consume(",")) break;
                }
                inTemplateArgs_ = wasArgs;
                takeClosingAngle();
            }
            const std::vector<std::size_t> *set = overloadsOf(qualifyForLookup(full, &Parser::hasFunctionNamed));
            const bool templ = !targs.empty() || fnTemplates_.count(leaf) != 0;
            if (!templ && set != nullptr && set->size() == 1 && !functions_[(*set)[0]].fromTemplate) {
                n.op = DepExpr::ExternalName;
                n.name = functions_[(*set)[0]].symbol;
            } else if (templ || set != nullptr) {
                n.op = DepExpr::QualifiedName;
                n.scope = scope;
                n.name = leaf;
                n.targs = targs;
            } else {
                depRefuse(pos, "the qualified name '" + full + "', which names no function,");
            }
            return depNode(n);
        }
        // A type parameter: `T(x)` is a cast, `T::count` the unresolved `sr T_ 5count`.
        if (boundTypeParams_.count(name) != 0) {
            const Type *t = findTypedef(name);
            if (t != nullptr && t->kind() == Kind::TemplateParam) {
                if (peekAt(1).is("::")) {
                    if (peekAt(2).kind != TokenKind::Ident || peekAt(3).is("::") ||
                        peekAt(3).is("<"))
                        depRefuse(pos, "a qualified name other than 'T::name'");
                    n.op = DepExpr::QualifiedParamName;
                    n.type = t;
                    n.name = peekAt(2).text;
                    at_ += 3;
                    return depNode(n);
                }
                at_++;
                return depCast(t);
            }
        }
        auto np = nonTypePatternParams_.find(name);
        if (np != nonTypePatternParams_.end()) {
            at_++;
            n.op = DepExpr::TemplateParam;
            n.index = np->second;
            return depNode(n);
        }
        // An enclosing template's non-type parameter is bound to its value here, and clang spells
        // that value: `V<N + K>` in `V<3>` is `XplLm3ET_E`, measured.
        if (boundValueParams_.count(name) != 0) {
            const EnumConst *e = findEnum(name);
            if (e != nullptr && e->paramType != nullptr && e->paramType->isInteger()) {
                at_++;
                n.op = DepExpr::Literal;
                n.type = e->paramType;
                n.value = e->value;
                return depNode(n);
            }
        }
        if (const Local *l = findLocal(name)) {
            if (l->trailingParam < 0) depRefuse(pos, "a local name");
            at_++;
            n.op = DepExpr::FunctionParam;
            n.index = l->trailingParam;
            return depNode(n);
        }
    }
    if (atTypeName()) {
        StorageClass sc;
        return depCast(specifiers(&sc));
    }
    if (tok.kind == TokenKind::Ident) {
        if (peekAt(1).is("<") && findTemplate(tok.text) != templates_.end())
            depRefuse(pos, "a template-id");
        if (peekAt(1).is("::")) depRefuse(pos, "a qualified name");
        if (peekAt(1).is("(")) {
            at_++;
            n.op = DepExpr::UnresolvedName;
            n.name = tok.text;
            return depNode(n);
        }
        depRefuse(pos, "a name declared outside the template ('" + tok.text + "')");
    }
    depRefuse(pos, "'" + tok.text + "'");
    return nullptr;
}

// The tokens of [from, to), as a diagnostic shows the expression.
std::string Parser::depText(std::size_t from, std::size_t to) const {
    std::string text;
    for (std::size_t i = from; i < to; i++) {
        if (!text.empty()) text += ' ';
        text += tokens_[i].text;
    }
    return text;
}
