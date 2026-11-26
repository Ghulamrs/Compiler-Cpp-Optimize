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

// The one-letter Itanium code of a fundamental type: how `new` names its size_t.
const char *itaniumBuiltinCode(Kind k);

// A type as a signature spells it - `P1S`, `PKc` - the string a type_info holds.
bool itaniumTypeSpelling(const Type *t, std::string *out, std::string *problem);

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

// cl's vcall thunk for a virtual function taken by `&S::f`: `??_9S@@$B7AA`
// for the slot 8 bytes into the vftable - the class as a scope, `$B`, the
// offset as a number, and `AA` for the flat convention. Measured.
bool microsoftVcallThunkName(const Type *cls, int offset, std::string *out,
                             std::string *problem);

bool microsoftClassRttiNames(const Type *cls, MicrosoftRtti *out,
                             std::string *problem);

// **The chain a Microsoft throw hands the runtime**: a type descriptor and a
// catchable-type record (displacement, size, copy constructor) per type the object
// may be caught as, the array of those, and the ThrowInfo naming array and destructor.
struct MicrosoftThrow {
    struct Catchable {
        const Type *cls = nullptr;   // the class, for the parser to find its copy constructor
        std::string descriptor;      // ??_R0?AUBase@@@8
        std::string decorated;       // .?AUBase@@
        std::string name;            // _CT??_R0?AUBase@@@8??0Base@@QEAA@AEBU0@@Z4
        std::string copyCtor;        // filled by the parser, empty for a trivial copy
        int size = 0, mdisp = 0;
        bool simple = false;         // a scalar: CT_IsSimpleType
    };
    std::string descriptor;          // the thrown or caught type's own
    std::string decorated;
    std::vector<Catchable> catchables;
    std::string array;               // _CTA2?AUE@@
    std::string info;                // _TI2?AUE@@, _TI2CPEAD for a pointer to const (cl)
    std::string destructor;          // filled by the parser
    int size = 0;
    bool isConst = false;
    bool thrown = false;             // a type only caught needs its descriptor and nothing else
};
bool microsoftThrowNames(const Type *t, int size, MicrosoftThrow *out,
                         std::string *problem);
// The record names, once the parser has filled the copy constructors.
void microsoftThrowFinish(MicrosoftThrow *out);
// The descriptor `typeid` names: `??_R0H@8`, `??_R0?AUD@@@8`, `??_R0PEBD@8` - a pointer keeps its pointee's const here, where a throw's record drops it.
bool microsoftTypeidNames(const Type *t, MicrosoftThrow *out, std::string *problem);

bool microsoftFunctionName(const std::string &name, const Type *fn, bool internal,
                           std::string *out, std::string *problem);

// A non-static member function. Both ABIs spell the class in and record a const
// `this`; **the Microsoft name carries the access and the Itanium one does not**.
// `clsType` feeds Itanium's substitution table, where the class is candidate S_.
bool itaniumMemberName(const std::string &cls, const Type *clsType,
                       const std::string &name,
                       const Type *fn, unsigned cvThis, RefQual refThis,
                       std::string *out, std::string *problem);

bool microsoftMemberName(const std::string &cls, const Type *clsType,
                         const std::string &name,
                         const Type *fn, char access, unsigned cvThis, RefQual refThis,
                         std::string *out, std::string *problem);

// A member *function template* specialization: the member name with its own
// template arguments, inside the class.
bool itaniumMemberTemplateName(const std::string &cls, const Type *clsType,
                               const std::string &name, const Type *fn,
                               const std::vector<TemplateArg> &args,
                               unsigned cvThis, RefQual refThis,
                               std::string *out, std::string *problem);

bool microsoftMemberTemplateName(const std::string &cls, const Type *clsType,
                                 const std::string &name, const Type *fn,
                                 const std::vector<TemplateArg> &args,
                                 char access, unsigned cvThis, RefQual refThis,
                                 std::string *out, std::string *problem);

// A member function of a class defined inside a function body: both ABIs wrap the
// enclosing function's whole name round the ordinary one, `owner` being that
// name. One with no decorated name is `4main` to Itanium and `?main@@9` to cl.
bool itaniumLocalMemberName(const std::string &owner, const std::string &cls,
                            const Type *clsType, const std::string &name,
                            const Type *fn, unsigned cvThis, RefQual refThis,
                            std::string *out, std::string *problem);

bool microsoftLocalMemberName(const std::string &owner, const std::string &cls,
                              const Type *clsType, const std::string &name,
                              const Type *fn, char access, unsigned cvThis, RefQual refThis,
                              std::string *out, std::string *problem);

// A constructor template specialization: C1/C2 and `??$?0`, the arguments as a template-id.
bool itaniumConstructorTemplateName(const std::string &cls, const Type *clsType,
                                    const Type *fn, const std::vector<TemplateArg> &args,
                                    bool complete, std::string *out, std::string *problem);
bool microsoftConstructorTemplateName(const std::string &cls, const Type *clsType,
                                      const Type *fn, const std::vector<TemplateArg> &args,
                                      char access, std::string *out, std::string *problem);

// A constructor. **Itanium gives one constructor two names** - C1 complete and C2
// base - and clang emits both, so both are emitted here though only C1 is spelled.
// Microsoft has one, ??0, writing '@' where a member function writes its return.
bool itaniumConstructorName(const std::string &cls, const Type *clsType,
                            const Type *fn,
                            bool complete, std::string *out, std::string *problem);
// `using B::B;`: D's constructor inherited from B, spelled CI1 or CI2 and then B's type.
bool itaniumInheritingConstructorName(const std::string &cls, const Type *clsType,
                                      const Type *base, const Type *fn, bool complete,
                                      std::string *out, std::string *problem);

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

// **An adjustor thunk** (Phase 6): `this -= adjust` and on to a virtual function, named as the function
// with `W` for its `U` and the adjustment after it - ?b@X@@W7EAAHXZ, ??_GX@@WBA@EAAPEAXI@Z, as cl writes.
// `vtordisp`: the thunk reads the vtordisp in front of the virtual base first, `$4PPPPPPPM@...`.
bool microsoftAdjustorThunkName(const std::string &cls, const Type *clsType, const std::string &name,
                                const Type *fn, bool constThis, RefQual refThis, long long adjust,
                                std::string *out, std::string *problem, bool vtordisp = false);
std::string microsoftDeletingDestructorThunkName(const std::string &cls, const Type *clsType,
                                                 long long adjust, bool vtordisp = false);
// The bases naming one of a class's vftables, `I2@@` in `??_7X@@6BI2@@@`; empty for `??_7X@@6B@`.
std::string microsoftVftablePath(const std::vector<const Type *> &path);
// **One row of a class's _RTTIBaseClassArray** (Phase 6): the class or a base at any depth, in cl's
// order, where it sits, how many of its own bases follow it, its attributes, and the descriptor's name
// that spells all of them - ??_R1BA@?0A@EA@I2@@8. False where a virtual or repeated base is met.
struct MicrosoftBaseClass {
    const Type *cls = nullptr;
    int mdisp = 0, contained = 0, attributes = 0x40;
    int pdisp = -1, vdisp = 0;      // a virtual base's: the vbptr it is read through, its entry there
    std::string name;
};
std::string microsoftBaseDescriptorName(const MicrosoftBaseClass &row);
bool microsoftBaseClassArray(const Type *cls, std::vector<MicrosoftBaseClass> *out,
                             int *hierarchyAttributes, std::string *problem);
// A class's RTTI rows: that array where it can be, else its first-base chain
std::vector<MicrosoftBaseClass> microsoftRttiRows(const Type *cls, int *hierarchyAttributes);

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
std::string vbtableSymbol(const std::string &tag, const std::string &base,
                          const std::string &via = std::string());
// The vbase destructor: Itanium's D1 under a Microsoft name. `??1` destroys
// the class's own part; this one calls it and then the virtual bases.
std::string vbaseDestructorSymbol(const std::string &tag);
std::string itaniumClassNameString(const Type *cls);
std::string itaniumClassTypeInfoSymbol(const Type *cls);
std::string itaniumClassTypeNameSymbol(const Type *cls);
