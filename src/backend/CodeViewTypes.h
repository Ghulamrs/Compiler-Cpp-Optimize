#pragma once

// **CodeView's type stream, `.debug$T`**: every type a symbol in `.debug$S` names,
// numbered from 0x1000 in the order the records are written. A type a record refers
// to is always written before it, so each is built depth first on first use.

// A fundamental type, and a pointer to one, needs no record: CodeView numbers them
// itself (T_INT4 0x74, T_64PINT4 0x674, ...), and cl uses those numbers, which is what
// makes cdb print `int` and `int *` the way it does for cl's own programs.

// Records are kept as bytes, not directives: nothing in a type is relocated. A class
// is written as a forward reference first, so a member that points back at it can be
// numbered, then its field list, then the class itself - cl's order, and cdb's.

#include "../Type.h"

#include <map>
#include <string>
#include <vector>

class CodeViewTypes {
public:
    explicit CodeViewTypes(const Target &target) : target_(target) {}

    // The index for a type: a fundamental's own number, or a record's from 0x1000.
    unsigned index(const Type *t);
    // LF_PROCEDURE for a function returning `returns` and taking `params`.
    unsigned procedure(const Type *returns, const std::vector<const Type *> &params, bool variadic);

    // Every class, union and enum named so far, for S_UDT.
    const std::vector<std::pair<std::string, unsigned> > &named() const { return named_; }

    // `.debug$T`, whole; nothing when no record was needed.
    void write(std::string &out) const;

private:
    typedef std::vector<unsigned char> Bytes;

    unsigned record(int kind, const Bytes &body);
    unsigned fundamental(const Type *t) const;
    unsigned pointer(const Type *to, int mode);
    unsigned aggregate(const Type *t);
    unsigned enumeration(const Type *t);

    const Target &target_;
    std::vector<Bytes> records_;
    std::map<const Type *, unsigned> done_;
    std::map<std::string, unsigned> procedures_;
    std::vector<std::pair<std::string, unsigned> > named_;
};
