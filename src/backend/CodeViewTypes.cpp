#include "CodeViewTypes.h"

namespace {

// Leaf kinds (cvinfo.h).
const int kLfModifier = 0x1001, kLfPointer = 0x1002, kLfProcedure = 0x1008;
const int kLfArglist = 0x1201, kLfFieldList = 0x1203, kLfBitfield = 0x1205;
const int kLfBClass = 0x1400, kLfEnumerate = 0x1502, kLfArray = 0x1503;
const int kLfClass = 0x1504, kLfStructure = 0x1505, kLfUnion = 0x1506, kLfEnum = 0x1507;
const int kLfMember = 0x150d, kLfULong = 0x8004, kLfLong = 0x8003;

// Fundamental indices, and the 64-bit pointer mode that makes a pointer to one.
const unsigned kTNoType = 0x0000, kTVoid = 0x0003, kTUQuad = 0x0023, kT64Pointer = 0x0600;

// LF_POINTER attributes: CV_PTR_64 in bits 0-4, the mode in 5-7, the size in 13-18.
const unsigned kPtr64 = 0x0c, kPtrSize8 = 8u << 13;
const int kModePointer = 0, kModeLValueRef = 1, kModeRValueRef = 4;
const int kPropForwardRef = 0x80;

typedef std::vector<unsigned char> Bytes;

void u8(Bytes &b, unsigned v) { b.push_back(static_cast<unsigned char>(v)); }
void u16(Bytes &b, unsigned v) { u8(b, v & 0xff); u8(b, (v >> 8) & 0xff); }
void u32(Bytes &b, unsigned v) { u16(b, v & 0xffff); u16(b, v >> 16); }
void text(Bytes &b, const std::string &s) {
    for (char c : s) u8(b, static_cast<unsigned char>(c));
    u8(b, 0);
}

// A numeric leaf: the value itself below 0x8000, else a leaf kind and four bytes.
void numeric(Bytes &b, long long v) {
    if (v >= 0 && v < 0x8000) { u16(b, static_cast<unsigned>(v)); return; }
    u16(b, v < 0 ? kLfLong : kLfULong);
    u32(b, static_cast<unsigned>(v));
}

// To four, with LF_PAD bytes that say how many are left (0xf3, 0xf2, 0xf1).
void pad(Bytes &b) {
    while (b.size() % 4 != 0) u8(b, 0xf0 + (4 - b.size() % 4));
}

int accessOf(Access a) { return a == Access::Private ? 1 : a == Access::Protected ? 2 : 3; }

}

unsigned CodeViewTypes::record(int kind, const Bytes &body) {
    Bytes r;
    u16(r, 0);
    u16(r, static_cast<unsigned>(kind));
    r.insert(r.end(), body.begin(), body.end());
    pad(r);
    const unsigned length = static_cast<unsigned>(r.size() - 2);
    r[0] = static_cast<unsigned char>(length & 0xff);
    r[1] = static_cast<unsigned char>(length >> 8);
    records_.push_back(r);
    return 0x1000 + static_cast<unsigned>(records_.size()) - 1;
}

// cl's numbers: plain char is T_RCHAR (0x70), long long is T_QUAD - which cdb prints "int64".
unsigned CodeViewTypes::fundamental(const Type *t) const {
    switch (t->kind()) {
    case Kind::Void:      return kTVoid;
    case Kind::Bool:      return 0x30;
    case Kind::Char:      return 0x70;
    case Kind::SChar:     return 0x10;
    case Kind::UChar:     return 0x20;
    case Kind::Short:     return 0x11;
    case Kind::UShort:    return 0x21;
    case Kind::WChar:     return 0x71;
    case Kind::Char16:    return 0x7a;
    case Kind::Char32:    return 0x7b;
    case Kind::Int:       return 0x74;
    case Kind::UInt:      return 0x75;
    case Kind::Long:      return 0x12;
    case Kind::ULong:     return 0x22;
    case Kind::LongLong:  return 0x13;
    case Kind::ULongLong: return 0x23;
    case Kind::Float:     return 0x40;
    case Kind::Double: case Kind::LongDouble: return 0x41;
    case Kind::NullPtr:   return kT64Pointer | kTVoid;
    default:              return kTNoType;
    }
}

unsigned CodeViewTypes::pointer(const Type *to, int mode) {
    const bool plain = to == nullptr || (fundamental(to) != kTNoType && !to->isConst() &&
                                         !to->isEnumeration() && !to->isNullPtr());
    if (mode == kModePointer && plain) return kT64Pointer | (to ? fundamental(to) : kTVoid);
    Bytes b;
    u32(b, index(to));
    u32(b, kPtr64 | (static_cast<unsigned>(mode) << 5) | kPtrSize8);
    return record(kLfPointer, b);
}

unsigned CodeViewTypes::procedure(const Type *returns, const std::vector<const Type *> &params,
                                  bool variadic) {
    std::vector<unsigned> args;
    for (const Type *p : params) args.push_back(index(p));
    // A variadic list ends in T_NOTYPE, as cl writes `...`.
    if (variadic) args.push_back(kTNoType);
    const unsigned ret = index(returns);
    std::string key = std::to_string(ret);
    for (unsigned a : args) key += "," + std::to_string(a);
    std::map<std::string, unsigned>::const_iterator seen = procedures_.find(key);
    if (seen != procedures_.end()) return seen->second;

    Bytes list;
    u32(list, static_cast<unsigned>(args.size()));
    for (unsigned a : args) u32(list, a);
    const unsigned arglist = record(kLfArglist, list);
    Bytes b;
    u32(b, ret);
    u8(b, 0);
    u8(b, 0);
    u16(b, static_cast<unsigned>(args.size()));
    u32(b, arglist);
    return procedures_[key] = record(kLfProcedure, b);
}

// **Forward reference, field list, then the class**; a member pointing back at it gets the first.
unsigned CodeViewTypes::aggregate(const Type *t) {
    const Type *c = &t->cls();
    const bool isUnion = c->kind() == Kind::Union;
    const int kind = isUnion ? kLfUnion : c->declaredClass() ? kLfClass : kLfStructure;
    const std::string name = c->tag().empty() ? std::string("<unnamed-tag>") : c->tag();

    // count, property, then (not for a union) derived-from and vshape, size, name.
    auto head = [&](unsigned count, unsigned property, unsigned fields, long long size) {
        Bytes b;
        u16(b, count);
        u16(b, property);
        u32(b, fields);
        if (!isUnion) { u32(b, 0); u32(b, 0); }
        numeric(b, size);
        text(b, name);
        return b;
    };
    const unsigned forward = record(kind, head(0, kPropForwardRef, 0, 0));
    done_[c] = forward;
    if (!c->isComplete()) return forward;

    Bytes fields;
    unsigned count = 0;
    for (const Type::BaseSpec &base : c->bases()) {
        if (!base.direct || base.isVirtual) continue;
        u16(fields, kLfBClass);
        u16(fields, static_cast<unsigned>(accessOf(base.access)));
        u32(fields, index(base.type));
        numeric(fields, base.offset);
        pad(fields);
        count++;
    }
    for (const Member &m : c->members()) {
        if (m.declaredIn != nullptr || m.inVirtualBase != nullptr) continue;
        unsigned type = index(m.type);
        if (m.isBitField()) {
            Bytes bf;
            u32(bf, type);
            u8(bf, static_cast<unsigned>(m.width));
            u8(bf, static_cast<unsigned>(m.bitOffset));
            type = record(kLfBitfield, bf);
        }
        u16(fields, kLfMember);
        u16(fields, static_cast<unsigned>(accessOf(m.access)));
        u32(fields, type);
        numeric(fields, m.offset);
        text(fields, m.name);
        pad(fields);
        count++;
    }
    const unsigned list = record(kLfFieldList, fields);
    const unsigned full = record(kind, head(count, 0, list, c->size(target_)));
    done_[c] = full;
    named_.push_back(std::make_pair(name, full));
    return full;
}

// The enumerators are not kept on the type, so the list is empty: cdb shows the number.
unsigned CodeViewTypes::enumeration(const Type *t) {
    const unsigned list = record(kLfFieldList, Bytes());
    Bytes b;
    u16(b, 0);
    u16(b, 0);
    u32(b, fundamental(t));
    u32(b, list);
    text(b, t->enumTag());
    const unsigned e = record(kLfEnum, b);
    named_.push_back(std::make_pair(t->enumTag(), e));
    return e;
}

unsigned CodeViewTypes::index(const Type *t) {
    if (t == nullptr) return kTVoid;
    std::map<const Type *, unsigned>::const_iterator seen = done_.find(t);
    if (seen != done_.end()) return seen->second;

    unsigned n;
    if (t->isArray()) {
        Bytes b;
        u32(b, index(t->pointee()));
        u32(b, kTUQuad);
        numeric(b, t->length() > 0 ? static_cast<long long>(t->size(target_)) : 0);
        text(b, "");
        n = record(kLfArray, b);
    } else if (t->isConst() && t->unqualified() != t) {
        Bytes b;
        u32(b, index(t->unqualified()));
        u16(b, 1);
        n = record(kLfModifier, b);
    } else if (t->isPointer()) {
        n = pointer(t->pointee(), kModePointer);
    } else if (t->isReference()) {
        n = pointer(t->referent(), t->isRValueReference() ? kModeRValueRef : kModeLValueRef);
    } else if (t->isFunction()) {
        n = procedure(t->returns(), t->params(), t->isVariadicFn());
    } else if (t->isStructOrUnion() && !t->isMemberFunctionPointer()) {
        return aggregate(t);
    } else if (t->isEnumeration()) {
        n = enumeration(t);
    } else {
        n = fundamental(t);
    }
    done_[t] = n;
    return n;
}

void CodeViewTypes::write(std::string &out) const {
    if (records_.empty()) return;
    out += "  .section .debug$T,\"dr\"\n  .p2align 2\n  .long 4\n";
    for (const Bytes &r : records_) {
        for (std::size_t i = 0; i < r.size(); i += 16) {
            out += "  .byte ";
            for (std::size_t k = i; k < r.size() && k < i + 16; k++) {
                if (k > i) out += ", ";
                out += std::to_string(r[k]);
            }
            out += '\n';
        }
    }
}
