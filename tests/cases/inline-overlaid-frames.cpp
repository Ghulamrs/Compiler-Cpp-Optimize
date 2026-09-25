// Two callees walked in place at the same site share the frame below the
// caller's locals. `advance` keeps a 64-byte Token there - the one nextToken
// returns, copied a word at a time into `cur` - and BinaryExpr's constructor
// keeps its pointer parameters there, over the words holding line and col.
// At -O2 those parameters are scalars offered a register; the optimizer
// once took them as the whole story of their bytes, and the Token's copy
// read line and col from registers nothing had written.
//
// Found in Compiler++'s Parser::parseAddSub: the second error's position
// came out as `189:-983567408`. The expected output is clang's.
#include <string>
extern "C" { int printf(const char *, ...); }

struct Token {
    int kind;
    std::string text;
    long number;
    double value;
    bool suffixed;
    int line;
    int col;
    Token() : kind(0), number(0), value(0.0), suffixed(false), line(0), col(0) {}
};

struct Lexer {
    int at;
    Lexer() : at(0) {}
    Token nextToken() {
        Token t;
        t.kind = at < 3 ? 1 : 0;
        t.text = "+";
        t.line = 4;
        t.col = 15 + at;
        ++at;
        return t;
    }
};

struct Expr {
    int line, col;
    Expr() : line(0), col(0) {}
    virtual ~Expr() {}
};

struct BinaryExpr : Expr {
    int op;
    Expr *lhs;
    Expr *rhs;
    void *resolved;
    BinaryExpr(int o, Expr *l, Expr *r) : op(o), lhs(l), rhs(r), resolved(0) {}
};

Expr *leaf() { return new Expr(); }

struct Parser {
    Token cur;
    Lexer *lexer;
    Parser(Lexer *l) : lexer(l) { advance(); }
    void advance() { cur = lexer->nextToken(); }
    Expr *parseAddSub() {
        Expr *left = leaf();
        while (left && cur.kind == 1) {
            const int op = cur.col & 1;
            const int line = cur.line, col = cur.col;
            advance();
            Expr *e = new BinaryExpr(op, left, leaf());
            e->line = line;
            e->col = col;
            left = e;
        }
        return left;
    }
};

int main() {
    Lexer lx;
    Parser p(&lx);
    Expr *e = p.parseAddSub();
    for (BinaryExpr *b = static_cast<BinaryExpr *>(e); b != 0; ) {
        printf("%d:%d op %d\n", b->line, b->col, b->op);
        Expr *next = b->lhs;
        delete b->rhs;
        b->rhs = 0;
        b->lhs = 0;
        delete b;
        b = next->line != 0 ? static_cast<BinaryExpr *>(next) : 0;
        if (b == 0) delete next;
    }
    return 0;
}
