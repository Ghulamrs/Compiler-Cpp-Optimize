#ifndef CXX1_PARSER_INTERNAL_H
#define CXX1_PARSER_INTERNAL_H

// The parser is one class over the files in this directory, and this is the
// little that has to cross between them: the helpers whose callers sit on both
// sides of a seam, which is why they are not `static`. CLAUDE.md keeps why.

#include "../Ast.h"

#include <string>

// Round `n` up to a multiple of `a`.
int alignTo(int n, int a);

// Where `base`'s subobject sits inside `derived`, or -1 if it is not a public base of it at all.
int publicBaseOffset(const Type *derived, const Type *base);

// Does this expression name an object with an address of its own?
bool isLvalue(const Expr &e);

// Does it name an object, lvalue or xvalue alike?
bool isGlvalue(const Expr &e);

// Is this an lvalue as the object of a member call - not a temporary, not what a move hands over?
bool objectIsLvalue(const Expr &e);

// The integer literal 0, which [conv.ptr] lets stand for a null pointer.
bool isNullConstant(const Expr &e);

// The keyword, if `word` is one the lexer knows and this parser has no rule for.
const char *notYetSupported(const std::string &word);

// The keyword, if it is one this parser *does* implement and this is not a place it may stand.
const char *implementedElsewhere(const std::string &word);

// How a BinOp is written in source, which is what `operator` is followed by when one is overloaded.
const char *binOpSpelling(BinOp op);

#endif
