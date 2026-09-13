#pragma once

#include "Type.h"

#include <string>
#include <vector>

// The linkage name of something with C++ linkage, in the platform's own ABI -
// which is what lets clang and cl be oracles at the object level; every rule was
// measured. `internal` is `static`: Itanium writes an L for it, Microsoft not.
bool itaniumFunctionName(const std::string &name, const Type *fn, bool internal,
                         std::string *out, std::string *problem);

// TemplateArg lives in Type.h: a specialization carries its arguments.

// A function template specialization, where the two ABIs want different things.
// **Itanium is given the template's pattern**, since it spells `T_` and encodes
// a return type; **Microsoft the substituted signature**, with tables of its own.
bool itaniumTemplateFunctionName(const std::string &name, const Type *pattern,
                                 const std::vector<TemplateArg> &args,
                                 bool internal,
                                 std::string *out, std::string *problem);

bool microsoftTemplateFunctionName(const std::string &name, const Type *fn,
                                   const std::vector<TemplateArg> &args,
                                   std::string *out, std::string *problem);

// The one-letter Itanium code of a fundamental type - `j` for unsigned int,
// `m` for unsigned long - which is how operator new's name says its size_t.
const char *itaniumBuiltinCode(Kind k);

// The Itanium name of a type's `std::type_info`: `_ZTI` and then the type as a signature spells it.
bool itaniumTypeInfoName(const Type *t, std::string *out, std::string *problem);

// **What the Microsoft ABI wants before it will let you throw**: not one
// pointer but a chain of four objects each naming the next - _TI1H, _CTA1H,
// _CT??_R0H@84, ??_R0H@8 - measured from cl. Fundamental types only.
struct MicrosoftRtti {
    std::string descriptor;      // ??_R0?AUBase@@@8
    std::string decorated;       // .?AUBase@@
    std::string baseDescriptor;  // ??_R1A@?0A@EA@Base@@8
    std::string array;           // ??_R2Base@@8
    std::string hierarchy;       // ??_R3Base@@8
    std::string locator;         // ??_R4Base@@6B@
};

bool microsoftClassRttiNames(const Type *cls, MicrosoftRtti *out,
                             std::string *problem);

struct MicrosoftThrow {
    std::string descriptor;
    std::string catchable;
    std::string array;
    std::string info;
    std::string decorated;
    int size = 0;
};
bool microsoftThrowNames(const Type *t, int size, MicrosoftThrow *out,
                         std::string *problem);

bool microsoftFunctionName(const std::string &name, const Type *fn, bool internal,
                           std::string *out, std::string *problem);

// A non-static member function. Both ABIs spell the class in and record a const
// `this`; **the Microsoft name carries the access and the Itanium one does not**.
// `clsType` feeds Itanium's substitution table, where the class is candidate S_.
bool itaniumMemberName(const std::string &cls, const Type *clsType,
                       const std::string &name,
                       const Type *fn, bool constThis,
                       std::string *out, std::string *problem);

bool microsoftMemberName(const std::string &cls, const Type *clsType,
                         const std::string &name,
                         const Type *fn, char access, bool constThis,
                         std::string *out, std::string *problem);

// A member *function template* specialization: the member name with its own
// template arguments, inside the class.
bool itaniumMemberTemplateName(const std::string &cls, const Type *clsType,
                               const std::string &name, const Type *fn,
                               const std::vector<TemplateArg> &args,
                               bool constThis,
                               std::string *out, std::string *problem);

bool microsoftMemberTemplateName(const std::string &cls, const Type *clsType,
                                 const std::string &name, const Type *fn,
                                 const std::vector<TemplateArg> &args,
                                 char access, bool constThis,
                                 std::string *out, std::string *problem);

// A member function of a class defined inside a function body: both ABIs wrap the
// enclosing function's whole name round the ordinary one, `owner` being that
// name. One with no decorated name is `4main` to Itanium and `?main@@9` to cl.
bool itaniumLocalMemberName(const std::string &owner, const std::string &cls,
                            const Type *clsType, const std::string &name,
                            const Type *fn, bool constThis,
                            std::string *out, std::string *problem);

bool microsoftLocalMemberName(const std::string &owner, const std::string &cls,
                              const Type *clsType, const std::string &name,
                              const Type *fn, char access, bool constThis,
                              std::string *out, std::string *problem);

// A constructor. **Itanium gives one constructor two names** - C1 complete and C2
// base - and clang emits both, so both are emitted here though only C1 is spelled.
// Microsoft has one, ??0, writing '@' where a member function writes its return.
bool itaniumConstructorName(const std::string &cls, const Type *clsType,
                            const Type *fn,
                            bool complete, std::string *out, std::string *problem);

bool microsoftConstructorName(const std::string &cls, const Type *clsType,
                              const Type *fn, char access,
                              std::string *out, std::string *problem);

// A destructor: the same two-name split as a constructor, D1 complete and D2
// base, emitted for the same reason. Microsoft writes ??1. D0, the deleting
// destructor, belongs to polymorphic delete and so to rung 4.
bool itaniumDestructorName(const std::string &cls, const Type *clsType,
                           bool complete, std::string *out);

std::string microsoftDestructorName(const std::string &cls, const Type *clsType,
                                    char access);

// The deleting destructor - Itanium's D0 and Microsoft's ??_G - which lives in
// a vtable and is the one `delete p` through a base pointer reaches. No
// program writes it, so nothing else names it.
std::string itaniumDeletingDestructorName(const std::string &cls,
                                          const Type *clsType);

std::string microsoftDeletingDestructorName(const std::string &cls,
                                            const Type *clsType);

// The copy assignment operator. Itanium spells `operator=` as the code `aS` and
// writes no return type; Microsoft replaces `?name@` with `??4` and does write
// one. Measured, cl first: ??4Poly@@QEAAAEAU0@AEBU0@@Z and _ZN4PolyaSERKS_.
bool itaniumCopyAssignName(const std::string &cls, const Type *clsType,
                           const Type *fn, std::string *out, std::string *problem);

bool microsoftCopyAssignName(const std::string &cls, const Type *clsType,
                             const Type *fn, char access,
                             std::string *out, std::string *problem);

// A static data member: a global the class gave its name to. Itanium spells it
// like a member function without the parameters and without the access;
// Microsoft writes the access as a digit - 2 public, 1 protected, 0 private.
std::string itaniumStaticMemberName(const std::string &cls, const Type *clsType,
                                    const std::string &name);

bool microsoftStaticMemberName(const std::string &cls, const Type *clsType,
                               const std::string &name,
                               const Type *t, char access,
                               std::string *out, std::string *problem);

// A variable at namespace scope. The Microsoft ABI mangles it whatever its
// linkage; Itanium leaves an external one alone and marks an internal one,
// the same way it marks an internal function.
bool microsoftDataName(const std::string &name, const Type *t,
                       std::string *out, std::string *problem);

std::string itaniumDataName(const std::string &name, bool internal);

// A class's vtable, by tag.
std::string vtableSymbol(const std::string &tag, bool microsoft);

// A class's Itanium type_info, the string it points at, and the text of that
// string - one encoding under two prefixes, the same nested form the vtable
// symbol uses. `_ZTI4Base`, `_ZTS4Base`, and "4Base".
std::string itaniumClassNameString(const std::string &tag);
std::string itaniumClassTypeInfoSymbol(const std::string &tag);
std::string itaniumClassTypeNameSymbol(const std::string &tag);

// **The same four, for a class whose name is a template-id.**
std::string vtableSymbol(const Type *cls, bool microsoft);

// **A class's vbtable, which only the Microsoft ABI has.**
std::string vbtableSymbol(const std::string &tag);
// The same, for a class holding more than one.
std::string vbtableSymbol(const std::string &tag, const std::string &base);
// The vbase destructor: Itanium's D1 under a Microsoft name. `??1` destroys
// the class's own part; this one calls it and then the virtual bases.
std::string vbaseDestructorSymbol(const std::string &tag);
std::string itaniumClassNameString(const Type *cls);
std::string itaniumClassTypeInfoSymbol(const Type *cls);
std::string itaniumClassTypeNameSymbol(const Type *cls);
