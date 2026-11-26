#ifndef CXX1_VERSION_H
#define CXX1_VERSION_H

// **The release, in one place.**
//
// 1.24 (2026-10-21) is 1.23 - mended against clang on macOS - with what 1.10 found against cl on
// Windows: masm and link started without cmd, CPP11_DECLINES empty or 0 as unset, `volatile` said as a
// warning at its line, deduction inside a template-id matching the argument as it is, the UCRT's
// headers (time_t of eight bytes, _time64, _findfirst, <sys/stat.h>, <windows.h>, <direct.h>), the
// string members 1.23 lacked, <algorithm>'s last functions inside std, and the headers copied
// beside cpp11.exe.
//
// 1.25 (2026-10-21): a braced list inside the braces that call a constructor initialises the
// parameter it reaches - `const CMatrix<3, 3> X = { {1.0, 2.0, 3.0}, ... }` makes each row the
// CVector<3> of CMatrix(CVector<3>, CVector<3>, CVector<3>), as cl and clang++ -std=c++11 do - and a
// braced call argument goes to the overloads whose parameter such a list can initialise.
//
// 1.26 (2026-10-21): the first five fixes of the 1.25 review - a temporary bound to `T &&`, `auto &&`
// or `const T &` is the call's own object and lives to the reference's scope's end, a temporary a
// reference holds a member of is destroyed there too, a static local whose initialiser throws is
// built again on the next call, the implicit move assignment exists and a written copy assignment
// suppresses the implicit move constructor, and a 256 MB stack with the parser's nesting bounded by
// name where 80 parentheses or a 40-deep template recursion used to overflow it.
//
// 1.27 (2026-10-21): the 1.25 review's two remaining items. Breadth - overload ranking by cv and by
// the nearer base, a user-defined conversion's second standard conversion, deduction keeping a
// reference's const and seeing through `T (&)[N]`, `const T &&`, ADL on an enumeration's operators,
// a nested class reading the outer class's later and private members, a local class's members by
// the written name, `decltype` of a comparison, of a string literal and of `c ? x : cx`, `PB::v`
// and `Base<T>::v` through `this`, `a[arr]`, `return {4, 5}`, `S{"s", 1}` for an aggregate holding a
// std::string, `int a[]{...}`, `const int &r{9}`, `throw` as an expression, `__func__`, a UTF-8 BOM,
// `(1, 3)` as a constant, `-1 > 0u` in `#if`, `_Pragma`, a function-like macro's name at the end of
// an expansion rescanned with the tokens after it, pointers to member functions in arrays and against
// null, `&Outer::K`, `const int Outer::K;` holding its value, a lambda reading the enclosing
// function's constants, a member of an rvalue being an xvalue, and a function template's signature
// read in its own namespace. Library - vector::at and array::at throw out_of_range on every target,
// vector moves where the standard moves and takes a move-only element type, std::swap moves, a map's
// element is pair<const K, V> - its insert takes the pair<K, V> make_pair gives, and a pair<K, V> is
// made from it - std::declval.
//
// 1.28 (2026-10-22): the 1.25 review's wrong-code set. A static local of an inline, in-class or
// template function is one object for the program - in a COMDAT under cl's name, `?n@?1??f@@YAHXZ@4HA`
// with its `?$TSS0@...` guard, and under Itanium's `_ZZ1fvE1n` with `_ZGV`, so a cpp11 unit and a cl
// unit share it; `int *` and `const int *` thrown in one unit write their `_CTA2PEAH` once; a member
// template called with explicit arguments, `s.m<int>(1)`, keeps its definition in the unit; and three
// ill-formed programs are refused: `static_cast` between pointers to unrelated objects, an `explicit
// operator bool` in copy-initialisation, and `--` on a bool.
//
// 1.29 (2026-10-22): -g through GRIDE's own masm, the PDB line's cpp11 part. In the MASM spelling
// -g is no longer refused: the lines go out as the `.FILE n, "path"` and `.LINE line, n` directives
// GRIDE's masm reads, by the `.cv_loc` rules, and the CodeView the gnu spelling writes for clang -
// S_GPROC32 with the display name, parameters and locals, blocks, globals, the types - is spelled
// for masm as `.debug$S` and `.debug$T` SEGMENTs, a mergeable function's ASSOCIATIVE with its
// COMDAT; the driver assembles with the masm beside this program, and link /DEBUG makes the PDB.
// For the debugger that reads it: every name is whole, with its namespaces, as cl's PDB spells it -
// `std::basic_string<char>::size`, `geo::Box<int>::count`, `geo::Pair::Pair` - a template specialization
// with its arguments, `biggest<int>`, and a handler funclet is a procedure of its own with its lines,
// cl's `` `f'::`1'::catch$0 ``, so a breakpoint in a catch block lands on its line and the stack names
// the frame. Without -g the output is unchanged.
//
// 1.30 (2026-10-23): three fixes GRIDE's Windows runtime needs. `p - n` with n an integer narrower
// than the pointer - an `unsigned`, and `unsigned long` on Windows - widens n before it negates it:
// on every 64-bit target p - n was p + (2^32 - n) (RTS6x's `text_ + sizeof text_ - length_`); the
// C6000's output is unchanged. In the MASM spelling a C name that is a MASM keyword - `fabs`, C90's
// `div`, `length` - is the symbol's own name, given up with `OPTION NOKEYWORD:<...>` as ml64 has it,
// where a definition had been renamed `$fabs`; in such a file a `div` instruction is written as its
// bytes. And -g with a dynamic initialiser names the `$LNend$` and `$LNprolog$` labels its code
// defines, so GRIDE's masm takes the file.
//
// 1.31 (2026-10-28): Phase 1 of the road to strict C++11 (the 1.30 verification, Fable 5.1), measured
// by tests\conformance: the silent wrong code, the crashes and the four most dangerous accepted
// programs. A mem-initialiser's temporaries die at the end of it, the base's and the delegated
// constructor's too; `d.L::v` reaches a virtual base's member through L's own subobject; `throw x`
// moves from a local, a parameter, an xvalue or a prvalue where [class.copy]/31-32 allow it; `new
// T[n]` with n below zero or past a size_t throws std::bad_array_new_length; `decltype(cs.m)` is the
// member's declared type, a reference member's included; an enumeration is spelt by its own name
// in a specialization's key, so `k<E>` is not `k<int>` and `Box<E>` not `Box<int>`; a member
// initialiser in a class with no constructor reads `this` of the object being built; partial
// specializations are ordered with their packs, `A<T, U, Ts...>` against `A<T, T, Ts...>` no longer
// crashing; the register allocator demotes every node it cannot colour once a function has taken
// sixteen rounds, so a 3000-term expression compiles at -O2 in seconds, the colouring otherwise as
// it was. Refused now: a static_cast casting const away, an assignment through a copy assignment
// [class.copy]/23 deletes, a conversion to an ambiguous base, an `explicit` conversion function used
// by a built-in operator. CPP11_PASS_TIMES prints each optimiser pass's time.
//
// 1.32 (2026-10-28): W13, and the first of Phase 2's C++98 holes. On x86_64-windows every vbtable
// pointer of the complete object is set - those inside its virtual bases too - a shared one's table
// begins with its base's order, and two subobjects of one class get tables of their own; a virtual
// base with virtual bases of its own no longer crashes. Compiled now: a pure virtual destructor; a
// parameter of function type; `char s[] = {"ab"}`; `static_cast<B &>(d)`; a pointer to member, and
// `bool b = p`, where a bool is wanted; a local class of a member function reading its class's
// private members; `: m{v}` and `: B{v}` mem-initialisers, without narrowing; an exception
// specification on a pointer to function, held to [except.spec]/5 and refused in a typedef; a
// function template's default argument naming its parameters, a class template's member's too.
//
// 1.33 (2026-10-29): Phase 2's template parameters and member function types. A non-type template
// parameter may be a pointer, a reference, a pointer to member or std::nullptr_t, its type built on an
// earlier parameter's or not - `template <int *P>`, `template <void (&F)()>`, `template <int S::*PM>`,
// `template <class T, T *P>` - and its argument `&gv`, an array or a function named bare, `&S::m`, `&S::f`
// or a null pointer of its type, every use of the parameter rebuilt in the function that reads it; both
// ABIs name the specialization as cl and clang do, `??$deref@$1?gv@@3HA@@YAHXZ`, `_Z5derefIXadL_Z2gvEEEiv`.
// A floating or class type for one is refused where it is written. `int (S::*)(int) const` is a type of
// its own, `P8S@@EBAHH@Z` and `M1SKFiiE`: `&S::f` of a const f has it, a non-const pointer refuses it, and
// it is the only kind a const object is called through; an alias declaration reads one. `0` and `nullptr`
// reach a pointer to member as a call's argument.
//
// 1.34 (2026-10-29): `volatile` in the type system. A qualifier beside const on every type, `this`
// included: `volatile int *` is not `int *` and neither converts to the other's pointee without a cast,
// a reference to a non-volatile type binds no volatile object, `f() volatile` is a member of its own,
// overloads and deduction keep the qualifier (`const T &` given a `const volatile int` deduces T as
// `volatile int`), a `const volatile` object is no constant, and the class a compiler writes the copy
// constructor of is not copied from a volatile one. cl's and clang's names - `?g@@3HC`, `PECH`,
// `?h@S@@QECAHXZ`, `PVKi`, `_ZNVK1S1kEv` - and <type_traits>' is_volatile, remove_volatile, add_cv; a
// file that writes it is still compiled without -O, the optimiser's passes not yet asking it. And
// R5, R8, R11, R15, R22, R26 of the plan's Phase 2: the surrogate call through a conversion to a
// function pointer, default arguments added over declarations, `using N::f; using M::f;` as one
// overload set - `auto p = std::strlen;` with it - `M C::*` deduced, `T &&` given a string literal,
// `f(std::string &&)` chosen for `f("lit")`, `auto &rf = fn;`, a braced list bound to a reference to
// an array, an aggregate's reference members, and `alignas(T)` in a class template's head. Merged in
// with them: the preprocessor as translation phases 1-4 over pp-tokens, with hide sets and `#if` on
// intmax_t (Phase 3); on x86_64-windows a handler's own try, cleanups and rethrow as funclets inside
// its funclet, function-try-blocks, `dynamic_cast<T &>`, and two or more polymorphic bases with a table
// each, cl's adjustors and RTTI (Phase 6); and the library's C names, `abs` in its argument's type, and
// the manipulators on either stream (Phase 7a). With the last: C++ overloads beside the one function of a
// name with C linkage ([dcl.link]/6), so <stdlib.h>'s `abs(int)` keeps C's linkage and the TI and
// Windows runtimes that define it build again, and a redeclaration that changes C++ linkage to C is
// refused ([dcl.link]/5).
//
// 1.35 (2026-10-29): Phase 2's R6, R16, R20, R28. A conversion function template is deduced from the
// type a conversion asks for ([temp.deduct.conv]) - `template <class T> operator T *()` at `double *dp =
// s;` - and loses a tie to a function that is not one; a constexpr function template is made where a
// constant needs it (`Arr<sq(3)>`, `int a[sq(3)]`); `noexcept(e)` on a template is folded with its
// arguments; `new T[n]{a, b}` builds the listed elements where they live and destroys them if a later
// one throws, `new auto(e)` and `new T[n][3]` beside it. A template has no C linkage ([temp]/4),
// `extern "C++"` inside `extern "C"` is C++, `operator new` returns `void *`. Merged in with them: on
// x86_64-windows a polymorphic virtual base, its overriders entered at the base, and cl's vtordisp
// with its thunks (Phase 6); and the streams' iostate with a sentry, the unformatted and positioning
// members, and <string> to C++11 - char_traits, u16string, u32string, hash, every to_string (Phase 7a).
//
// 1.36 (2026-10-29): Phase 2's R7, R10, R12, R27. A member function or static data member of one
// specialization written out, `template <> int S<int>::f()` and `template <> int S<int>::s = 2;`, in
// place of the template's; explicit specializations of a deleted function template, whose own
// specializations are deleted; `template <class U> friend class H;` and friend function templates, and
// member alias templates, `S::M<int>`; a placement `new` whose constructor throws frees through the
// matching placement `operator delete` with the same arguments, `delete` takes the usual one beside it,
// and `::new` and `::delete` pass a class's own over. Merged in with them: on x86_64-windows thread_local,
// a noexcept function that ends the program when an exception leaves it, and `throw(T...)` honoured
// through unexpected() (Phase 6).
//
// 1.37 (2026-10-29): Phase 2's R21, its ranking half. A braced argument among overloads that take
// different types there is made for the one it converts to best ([over.ics.list], [over.ics.rank]/3):
// an initializer_list<X> before anything else, then the worse of its elements' conversions - `f({1})`
// takes f(initializer_list<int>) over f(int), `k({"s"})` takes `const char *` over std::string - and
// two that take it equally well are refused by name.
//
// 1.38 (2026-10-29): what Phase 2's R-list left open. `::g` and `::f()` are looked up in the global
// namespace alone ([basic.lookup.qual]/4); a C function declared in a namespace is reached through it
// ([dcl.link]/6); a literal class where an integral constant is wanted goes through its conversion
// function ([expr.const]/5), `P()` of a class with no constructor folds to zeroes, and `constexpr
// explicit` is read in either order; an overloaded name in a braced list is the one its element's type
// names ([over.over]/1).
//
// 1.39 (2026-10-29): Phase 2's R21, its other half. An initializer_list of class objects made inside an
// expression - `count({T(1), T(2)})` - has its elements as temporaries of the full expression, built
// in place and destroyed at its end, last first, or as far as they were built when one throws; an
// element made from a prvalue of its own class or through a converting constructor is constructed
// where it lives, the copy cl and clang leave out, in a declaration too.
//
// 1.40 (2026-10-29): a file that writes `volatile` keeps its -O on x86_64: each function that reads or
// writes a volatile lvalue is written past the optimizer and walks no callee in place, nor is it walked in
// another, and the rest of the file is optimized as before - the C6000 and arm64 still compile such a file
// without -O. Itanium names carry clang 17's `Tn<type>` before a non-type argument whose parameter's type
// is another parameter's, `_Z1fIiTnT_Li3EEiv`.
//
// 1.41 (2026-10-29): Phase 2's R7, its member templates ([temp.expl.spec]/16). `template <> template <>
// int S<int>::g(char)` is S<int>'s member template's specialization at char, found by every later use
// that deduces it, and `template <> template <class U> int S<char>::g(U)` defines S<char>'s anew; a
// member class of one specialization is still refused.
//
// 1.42 (2026-10-29): deduction from a braced list ([temp.deduct.call]/1, CWG 1591): `il({1, 2})` deduces
// initializer_list<T>'s T from the elements, `cnt({1, 2, 3})` a `T (&&)[N]`'s T and N, and any other
// parameter takes the list as a non-deduced context; the list is then made for the specialization's
// parameter, an array reference's as a temporary of the call.
//
// 1.43 (2026-10-30): Phase 4 begins. The type-trait built-ins clang and cl share - __is_class, __is_enum,
// __is_pod, __is_trivially_copyable, __is_constructible(T, Args...), __is_convertible_to, __is_assignable,
// __has_virtual_destructor and the rest, and __underlying_type(E) - which <type_traits> is written over.
// Names in a template are bound where it is written ([temp.res]/10): an unqualified call no longer sees an
// overload declared after the template, argument-dependent lookup still does, and a base that depends on a
// template parameter is left out of unqualified lookup ([temp.dep]/3). Phase 7a's <algorithm>, <numeric>
// and <limits>.
//
// 1.44 (2026-10-30): expressions over a template's parameters in a function template's signature - a
// `sizeof(T)` array bound, a qualified call, `std::declval<T>()`, a type trait - read as patterns and
// spelled as clang spells them; a function template only declared is a candidate; trailing template
// arguments a call does not write are deduced; explicit instantiation; a string literal bound to a reference
// to an array where it lies. Phase 5's first four batches (classes, initialisation, statements, unions,
// loops, friends, required diagnostics) and Phase 7a's <memory>, <utility> and <exception>.
//
// 1.45 (2026-10-30): member function templates defined outside their class, or only declared (the sizeof
// idiom), chosen among every member template of the name with substitution failure; partial specializations
// on an array's bound and on function types, a pack among the parameters, and a function type as a template
// argument (what std::function is written over); the [temp.mem] and [temp.names]/4 rules on member templates.
// Phase 5's batches 5-6 (access, constant expressions, template declarations) and Phase 7a's containers and
// <type_traits> over the built-ins.
//
// 1.46 (2026-10-30): member class templates (allocator::rebind) and member alias templates of class
// templates, named bare, qualified, through a base and after 'template'; a member class of a class
// template specialization declared and defined only where needed ([temp.inst]/1), so an explicit
// specialization of it may follow the class's first use. Phase 5's range-for over a temporary's member
// and Phase 7a's get<I> for pair and array, result_of and <type_traits> on 1.45's patterns.
//
// 1.47 (2026-11-02): packs expanded in base lists, mem-initializer lists, lambda captures, alignas, a call
// in a trailing return type and a template's fixed parameters; a function type's trailing pack deduced
// ([temp.deduct.type]/10); Tup<H, T...> in a partial specialization's arguments; member templates of a
// variadic class template; a function template named without a call ([over.over]/2); static auto members.
//
// 1.48 (2026-11-02): the wrong code the library found - a throwing constructor in placement new freed the
// storage, assignment through an empty base overwrote a member, a template in any namespace beat the named
// non-template - with argument-dependent lookup through bases and template arguments, implicit members'
// noexcept, typename-qualified non-type parameters, unnamed classes as template arguments; Phase 5's access
// to private member types and enumerators, friend-only functions, constexpr over arrays and addresses.
//
// 1.49 (2026-11-02): templates the program never instantiates are read once with stand-in arguments, their
// own errors said ([temp.res]/8-10) - Phase 4 finished; the library's compiler items: operator templates only
// where lookup reaches them, explicit and converting constructor templates, bodies of unchosen candidates
// left unsaid, ::operator new by name, initializer-list constructors with defaults, braced assignments.
//
// 1.50 (2026-11-02): Phase 7b begins - <functional> (function, bind, mem_fn, hash), <tuple>, <iterator> and
// <string>'s iterator ranges, with what they met: deduction from pointers to member functions, assignment to
// class prvalues, specializations only where deduced, copy-initialisation without explicit constructors,
// member templates ranked, pointers to members through template-ids, and class-to-scalar casts refused.
// 1.51 (2026-11-03): Phase 7b's containers - deque, list, forward_list, stack, queue, the unordered maps and
// sets, bitset, vector<bool> - with vector and basic_string<charT, traits, Allocator> going through
// allocator_traits, tuple's uses-allocator constructors and pair's piecewise one; and what they met: wrong
// code where `return os << s.str();` returned a copy's address and `B(std::move(d))` moved a copy, a trait
// asked inside a function clearing that function's slots, ADL through specializations, member templates'
// namespaces and partial ordering with packs, `void()`, and C++11's converting constructors with defaults.
// 1.52 (2026-11-03): Phase 7b finished - list, forward_list, deque and the unordered containers take their
// storage from the allocator, rebound, and build elements through allocator_traits; map, multimap, set and
// multiset keep an allocator and emplace in place, map::operator[] building its element piecewise; and what
// they met: template template parameters passed on and matched with packs, empty pack expansions, a
// template's own parameters hiding a template of their name, member defaults read in their class, trait
// trials leaving functions marked used, and packs lost across a nested inline body.
// 1.53 (2026-11-03): Phase 7c part 1 - <ratio>, <chrono>, <system_error> (ios_base::failure one), <cerrno>
// with all of [errno]'s names, <typeindex>, <complex>, <valarray>, <random> with the standard's engine
// values, <cmath>'s float, long double and integer overloads; and what they met: wrong code where `: m()`
// skipped a default constructor taking defaults, block typedefs that were global, short-circuit constants,
// constexpr bodies with typedefs, decltype as a nested name, and bound parameters in member templates.
// 1.54 (2026-11-03): Phase 7e - <atomic>, <mutex>, <condition_variable>, <thread> and <future> over the
// operating system's lock, condition variable and thread (atomics through one lock: the compiler emits no
// interlocked instruction) - and Phase 7d: tms6747 is a freestanding implementation, __STDC_HOSTED__ 0; the
// implicit object parameter ranked by its added qualifiers; nested #include to Annex B's 256.
// 1.55 (2026-11-03): the iostreams as the standard's templates - basic_streambuf, basic_ios, basic_istream,
// basic_ostream and the string and file streams over it, wide streams included, std::endl and its kin
// templates, one cout per program. Compiler: an overloaded name right of << and >> resolved by the member
// operator's parameter; a typedef of a specialization declared before its template's definition; a pointer
// to a specialization put off while its base is made; earlier declarations' defaults under their own names;
// a written template argument matched exactly in deduction; a base of a virtual base reached through the
// table (wrong code); template-id vbtable names.
// 1.56 (2026-11-03): <strstream> ([depr.str.strstreams]); a body compiled in the middle of another function -
// a constructor template made for an initialiser - keeps its entry-zeroed flags and its statements'
// temporaries in its own frame (wrong code: a std::function made in a variadic template wrote a parameter).
// 1.57 (2026-11-03): <locale> and <codecvt> - the locale with every standard facet, a stream's numbers through
// its num_put and num_get, imbue and getloc, wstring_convert and wbuffer_convert, codecvt_utf8/utf16; the
// time and money manipulators through their facets. Compiler: operator member templates defined outside a
// class; a protected base destructor in an implicit one; a default template argument read in its own
// namespace; a conditional's same-typed arms keep their type; escapes in wide literals are code units;
// <stdlib.h>'s MB_CUR_MAX.
// 1.58 (2026-11-04): <regex> - the six grammars on a backtracking machine, leftmost-longest for POSIX's, with
// regex_traits, sub_match, match_results, regex_match, regex_search, regex_replace and both iterators. Compiler:
// k::name in an expression is k's, not a class template of the namespace around it; a function template's
// default arguments are read in its namespace; a member's definition no longer reads its table entry after
// the body has grown the table (a crash).
// 1.59 (2026-11-04): a destructor that throws at the end of its scope runs once and the objects still alive are
// destroyed; a destructor with no specification is non-throwing, [except.spec]/14; a throw in a noexcept function
// calls std::terminate, not abort; noexcept(pf()) reads a pointer's own noexcept; throw of a prvalue builds the
// exception object in place. Language: pointer to member to bool and the boolean conversion's rank, friend T,
// function-type parameters, decltype of a function, T(4); as a temporary, lists of initializer_lists of two,
// overloaded function arguments decided by the other arguments, the lvalue/rvalue reference tie-break of
// [temp.deduct.partial]/9, packs of template template parameters, inherited constructor templates.
// 1.60 (2026-11-04): <scoped_allocator>. A throwing destructor leaves its object's other members, bases and array
// elements to be destroyed; a return in a destructor goes on to them; an initialiser's temporaries and object are
// destroyed under a throw. Friends defined in a namespace's class template are defined; noexcept on friends, data
// members and parameters; d.base::f() through a member typedef and a class's own private base by name; a trait no
// longer asks for a constructor template's body; tag dispatch rules a candidate out before its body; a reference
// binds a derived object with no constructor asked; an empty base is not copied over what follows it on the
// Microsoft layout. Census complete.
// 1.61 (2026-11-04): one name, one template per namespace - class templates, alias templates and function templates
// of a name in several namespaces, a program's own `vector` beside the library's. Template names are looked up:
// `vector<int>` with nothing bringing it here and `k::Box<int>` for a Box k has not are refused, two using-directives
// finding two templates are ambiguous; using-declarations of class templates; `::X<int>` and `::g(1)`. Release links
// by docs/Build_Policy.md: 7.45 MB to 4.73 MB.
// 1.62 (2026-11-05): the other three targets and Phase 8. By the Fable lane: every program with <iostream> compiles on
// the Itanium targets again, -O2 is -O2 on the C6747 and arm64-darwin past a header's `volatile`, wide file streams go
// through codecvt, Annex B's quantities are tested at the limit, IMPLEMENTATION-DEFINED.md. Three C6000 -O2 miscompiles
// that -O2 reached; the scheduler and a chain of bases in time linear in the program. atomic_flag lock-free on all four
// targets. Every error in a file, warnings, the instantiation chain. `typeid` of a pointer to member and delegation
// under virtual bases on Itanium, a null pointer under `typeid`, a class abstract through a later base; on Windows a
// pointer handler catches a thrown nullptr and every kind of pointer is thrown; va_arg of a class on all four.
// 1.63 (2026-11-10): the user's Sample14A - the C6000 passes in time linear in a block (MVKL/MVKH folded, copies
// forwarded and dropped in one pass each, a push and pop kept in A16-A31 found by lookup) and the x86-64 address
// folds stopped where nothing further down can refuse them, the code they write unchanged. With the runtimes review's
// fixes: an object with no constructor and a destructor is destroyed at exit, a binary operand calling setjmp is
// evaluated first on x86-64 Linux, and the driver's own link takes GRIDE's runtime when it is beside the compiler.
// 1.64 (2026-11-10): the runtimes review's open items (docs/Runtime_Design.md section 16). -g changes no instruction on
// x86-64: a call is walked in place under -g, its body on the call's line; what -g alone writes (locations, lexical
// blocks' labels, the function's begin label) reaches the optimizer as events marked debug, which make no block of
// the flow graph and stop no pass - so grt's Release libraries carry CodeView, their code the same byte for byte.
// The C6000 scheduler (C6xSched.cpp, C6xPipe.cpp) is c90's too. The warning "read here and nothing has been assigned
// to it" counts a local bound to a non-const reference as assigned by the call. -masm=masm: a file may name `dup`,
// the word given up and its zero fills written out.
// 1.65 (2026-11-11): two defects the comparison of the 1.63 and 1.64 installations found in both, by building every
// example. A function declared with a default argument and defined without one was refused - "every parameter after
// one with a default needs a default of its own" - when the function defined before it had used std::string: a
// top-level parameter list with no default left the last list read anywhere, an instantiated member's, to be merged
// into its own (cppparse's PathUtil.cpp, which 1.30 compiled; [dcl.fct.default]/4). And the C6000 scheduler let a
// double result's low word, which lands a cycle before its high word, reach a register in the same cycle as an earlier
// load into it - `LDBU *m, A4` then `INTDP A16, A5:A4` - where sim6747 stops (SPRUFE8 3.8.8; the examples' cpp-table
// at -O2): the write-after-write distance counts the early word, in a block, across blocks and in the pipeliner, for
// c90 as for cpp11. Conformance set p167 holds both, the second run on the C6747 by RTS6x's c6747.ps1.
// 1.66 (2026-11-11): the user's LabelIndex in the C6000 loop passes, joined to 1.65's scheduler (it was written on
// 1.64's). Where each label of the text stands and how many instructions name it are kept in an index, rebuilt when the
// text has been edited, where hoistInvariants, inductionPointers and widenFills scanned the whole text for every
// conditional branch and pipelineLoops for every branch; and pipelineLoops leaves a forward conditional branch before
// asking recognise and recogniseWindow, which both answered "the head is not above". A switch of 16,384 cases took
// 282 s for the C6747 at -O2 and takes 3 s. The code written is the same: 1,143 sources - the conformance programs,
// the examples and the runtime probes - give 1.65's C6747 assembly at -O2 byte for byte.
// 1.67 (2026-11-13): a cv-qualifier after a type's name. `T const &`, `std::size_t const n`, `S volatile s` - a
// `const` or `volatile` after a class, a typedef, a qualified name or a template parameter - were refused with
// "expected a name": the specifiers' reader took them after a built-in type in its own loop but returned as soon as
// it had any other, and left the qualifier for the declarator ([dcl.type]/1; open since the review of 1.62 noted it).
// And `const` or `volatile` written twice among one declaration's specifiers - `const int const`, `S volatile
// volatile` - is refused, where every release took the second silently ([dcl.type]/2); one a typedef holds is not
// a second. Conformance set p168: one program of every place the qualifier may follow a name, three refused.
// 1.68 (2026-11-13): what the conformance suite's first whole run on the C6747 found that no document listed
// (tests\conformance\run-conformance-c6747.ps1: 1,008 of 1,096 valid programs at 1.67). The difference of two
// pointers is of the type <cstddef> calls ptrdiff_t on every target - the C6747 gave `long` where its ptrdiff_t is
// `int`, so `decltype(q - p)`, an overload and a deduced argument each saw another type ([expr.add]/6). The type_info
// of a class that only a `throw(T...)` list names is emitted where the list is the unwinder's table, which names it -
// it was dropped as unused and the program did not link (q200 b33). And __STDCPP_THREADS__ is 1 on the hosted
// targets, not defined on the C6747 ([cpp.predefined]/2). Conformance set p169; EXCLUSIONS.md says which text is
// tracked, IMPLEMENTATION-DEFINED.md lists mbstowcs and nexttoward among what RTS6x lacks.
// 1.69 (2026-11-18): a handler's locals when an exception leaves the handler ([except.ctor]/1), on the Itanium targets and
// the C6000. A cleanup inside a handler hands over to the handler's own end and does not resume, so no enclosing cleanup
// runs after it - and its pad destroyed its own block's objects alone: `catch (int e) { T t(e); { T u(e); throw; } }` left
// t standing, and so did a `try` in a handler whose own handler threw or that caught nothing. A pad now destroys what
// the handler's block built since it was entered, as a `try` body's always did for the `try`; EXCLUSIONS.md loses the
// item (the suite's eh05 and eh09 on the C6747). And on x86_64-windows a handler's by-value parameter is destroyed
// when an exception leaves the handler - a throw from its block, or `throw;` ([except.handle]/16): the handler's
// funclet had no state for it, so `catch (E e) { throw 1; }` left e standing, on Microsoft's runtime and GRIDE's
// alike (found by p170's first program, which the C6747 passed and Windows did not). Conformance set p170.
// 1.70 (2026-11-18): several files in one command say how they get on (the user's: cppparse's 39 files were all
// started within five seconds and the screen then stood still for ten more, which reads as a compiler that hangs).
// A file says when it is done, with its seconds and the count so far - `[thread 4] done Sema.cpp - 9.8 s, 31 of 39` -
// and while files are compiling and nothing has been written for two seconds, a line says how many are done and
// which are in hand, the longest first. The threads are made three at a time, a set at work - each with its file in
// hand and said - before the next set is made, and none is made for work the earlier ones have taken; the assembler's
// runs the same. -nologo keeps all of it off, and a single file's output is as it was. No instruction changes.
// 1.71 (2026-11-18): an exception leaving a noexcept function calls std::terminate on the Itanium targets and the
// C6000 ([except.spec]/9, [except.terminate]/2). The pad that ends such a function called abort itself, so a handler
// set with set_terminate was never reached (the suite's nx01; a destructor's implicit noexcept the same, eh02).
// With RTS6x 1.5, whose unwinder now takes a throw(T...) list for the barrier it is, EXCLUSIONS.md loses the item.
// 1.72 (2026-11-18): on the C6000 a function that reads or writes a volatile object keeps its -O (the first
// measurement against TI's cl6x: 70.7 times its cycles on a register read in a loop, because such a function was
// written as at -O0, the whole of it - and on a DSP every device register is volatile). A volatile local stays in
// memory; the loads and stores of each expression that touches a volatile lvalue are marked in the text the
// scheduler reads (`*A4!v`), and the scheduler keeps a marked access as written: never the remake of another, never
// dropped where its value is unused, in its order among the marked ones, its loop left to run as written. Fewer marked
// accesses after the passes than before writes the function again as at -O0 (CPP11_VOLATILE_O0 asks for that way).
// tests\volatile holds thirty shapes of access whose loads and stores, counted and listed by sim6747 1.5's --watch,
// are the same at -O0, -O1 and -O2. Beside the master oracle, CCS 7.4's cl6x 8.2.2, they are the same but where it
// reads C++03's rules: it makes no read for a volatile lvalue whose value is discarded (`v;`, the left of a comma,
// a delay loop of them), which C++11 [expr]/11 makes one; and `b = ++v` it reads back, which cpp11 does not.
// 1.73 (2026-11-19): <tuple>'s constructors from a pair belong to a tuple of two elements ([tuple.cnstr]/17-22).
// They were candidates for any tuple, so forward_as_tuple(std::move(key)) of a key that is itself a pair - what
// map's operator[] makes of an rvalue key - took the pair apart instead of holding it, and
// `std::map<std::pair<int, std::vector<long> >, V> m; m[Key(a, b)]` was refused ("'__cxx1_head' is 'struct pair<...>'
// and this is 'int'"). Found by the cppparse example at cpp's 5f463e2 (eh\EhClFunclets.cpp); the suite's mk01.
// The headers alone change; the compiler's code is 1.72's.
// 1.74 (2026-11-19): two of what Fable 5.1's note on 1.62 and 1.73's own finding left.
// A member value-initialised in a constructor's list, `: m()`, was zeroed or had its default constructor run by an
// index into the functions, "none" being the count of functions when the list was read - and the members built
// before it add functions. A constructor template instantiated for an earlier member took that index, and
// `: first(std::forward<A>(a)...), second()` then called it on an int `second` and did not zero it: pair's
// piecewise constructor with a key that is a pair of a class, which is map's operator[] with such an rvalue key
// (the call was to a constructor nothing wrote out, so the program did not link - 1.73's exclusion, gone). "None"
// is a value of its own now. The suite's vi01.
// On the C6747 `typeid(*p)` tests p and a null one throws std::bad_typeid ([expr.typeid]/2) through
// `__cxa_bad_typeid`, as on the other Itanium targets: RTS6x has the routine since 1.3, and TI's own runtime has
// it (CCS 7.4, cl6x 8.2.2), which is what the compiler had waited to know. The exclusion goes.
// 1.75 (2026-11-19): <atomic> lock-free, which Fable 5.1's note on 1.62 had as the library's one item, and what
// its test found in passing.
// On x86-64 and arm64 an atomic scalar of 1, 2, 4 or 8 bytes - bool, the integers, an enumeration, a pointer,
// float, double - is lock-free: is_lock_free() true, the ATOMIC_*_LOCK_FREE macros 2. Its operations are sixteen
// functions the backend writes into each file that calls them, as it writes atomic_flag's exchange
// (`__cxx1_atomic_<load|xchg|add|cas>_<1|2|4|8>`, Spelling.h): `xchg`, `lock xadd` and `lock cmpxchg` on x86-64 in
// MASM's and AT&T's spellings, exclusive load-and-store pairs on arm64. and, or and xor are the compare-and-exchange
// retried; every order is given as seq_cst. A class, and every type on the C6747, stays under the one lock.
// Four threads on Windows count exactly with each size, and GRIDE's masm encodes the sixteen as ml64 does (but for
// the order of two prefixes); the Linux and arm64 texts are assembled by clang and not run, as all of those targets.
// `int x = a;` of a std::atomic<int> was refused: of two conversion functions to one type - <atomic> declares
// `operator T() const volatile` beside `operator T() const` - the compiler took neither. The one that fits the
// object is taken, the less qualified of two that fit ([over.ics.rank]/3).
// is_integral, and so is_arithmetic, is_fundamental and is_scalar, said false of wchar_t, char16_t and char32_t
// (atomic<char16_t> had no fetch_add), and is_signed and is_unsigned both said false of plain char and of those
// three; is_fundamental said false of std::nullptr_t. <type_traits> has them (a sweep of every unary trait over
// forty-eight kinds of type, beside cl's and clang's answers, found the last and no more).
// A like sweep of the relations and the transformations found two faults of the compiler's.
// cv brought to a reference by a typedef or a template argument was kept as a type of its own: `const T` with
// T = int & behaved as int & and was not the same type, so add_const<int &>::type was not int & ([dcl.ref]/1,
// [meta.trans.cv]). TypeTable gives a reference no cv.
// A type-id for a function that returns a pointer or reference to a function or an array, the name left out -
// `int (*(double))(double)` - was "expected ')'": the parenthesis after the `*` was read as one more parenthesised
// declarator, where it opens the parameter list. And `typedef int (*Picker(double))(double);` dropped that list
// and named the pointer the function returns.
// A third sweep, of tuples, pairs and function objects in use, found these.
// A reference was not bound through a class's conversion function to an lvalue ([dcl.init.ref]/5): `int &r =
// std::ref(n);` and `bump(std::ref(n))` were refused, a `const int &` took a copy, and make_tuple(std::ref(n))
// did not compile - reference_wrapper's whole use. bindReference binds what the function returns, and
// rankArgument counts it a user-defined conversion.
// In the headers: std::function made from or assigned nullptr was refused (its template took std::nullptr_t
// beside the constructor meant for it); tuple_cat took tuples only, not a pair; tuple_element of a const pair or
// tuple was refused, and tuple_size knew const alone; a tuple of an rvalue reference could not be copied where
// forward_as_tuple's is returned; std::hash of unique_ptr and shared_ptr was not there. is_trivially_constructible
// <int, C> said true where C's conversion function does the work.
// Left as it is, rare: is_nothrow_assignable<C, int> says true where the int is converted by a constructor that
// may throw (EXCLUSIONS.md).
// The suite's p175: at01, at02, tr01, cr01, dc01, rb01, tu01.
//
// 1.76 (2026-11-19): what a second round of sweeps beside cl found - the sequence containers, the associative and
// unordered ones with the adaptors, the algorithms, strings with their conversions and the streams, smart pointers
// with <chrono>, <ratio>, <random>'s engines and <regex>, threads with locks and futures. Six programs, each built
// by cl and by cpp11 and their outputs laid side by side; five faults, one of them silent.
// The silent one, in the x86-64 optimizer at -O2: a function that kept a pointer to function in a preserved
// register and called through it (`call *%r13`) had that register's save dropped as unused - dropUnusedSaves
// counted registers named as operands and as bases, not the one an indirect call goes through - so its caller's
// value in the register was lost. partition_copy, its predicate the sixth argument, was one such: a count computed
// before the call read 0 after it. Present since at least 1.73; -O0 and -O1 were right.
// A braced list as an argument, where an overload set holds a function and a specialization of a template beside
// it that take the same type, goes to the function ([over.match.best]/1): `m.insert({ "e", 5 })` after
// `m.insert(std::make_pair("d", 4))` was refused as ambiguous, the earlier call's specialization standing beside
// insert(const value_type &).
// A member function template hides a variable of the same name at namespace scope inside its class, as any member
// does: a program with a global `go` did not compile <future>, whose packaged_task has a member template `go`.
// In the headers: std::sort, stable or not, of move-only elements was refused (the pivot was copied; it is now
// followed in place); stoi, stol, stoul, stoll, stoull, stof, stod and stold on Windows printed a message and
// ended the program where [string.conversions]/2 has them throw invalid_argument and out_of_range.
// condition_variable::wait_until and wait_for said no_timeout for a wait that had timed out, once in twenty runs:
// the clock was asked afterwards, and the system's wait, counted in whole milliseconds, can end a fraction of one
// before the time point. The wait's own answer is used.
// On the C6747, in a function whose code passes 61 KB - its exception table is then in the 32-bit form - a handler
// was not found and the program ended in std::terminate when an earlier try block of the function had a piece
// with no instruction in it: a try is split around the regions inside it, such a piece was written with the
// length 0, and in that form a first word of 0 ends the table. Found by the strings sweep's main, 64 KB
// unoptimized; such a piece is now left out.
// And what a report from the field brought (CCS's SampleExt, Debug, "does not fit in SHRAM"): `<iostream>` made
// the four wide streams in every program - basic_filebuf<wchar_t>, the wide stream classes and the facets they
// reach, a quarter of the code of a program that prints with std::cout, 39 KB unoptimized on the C6747. They are
// made in a program that names one of them: `[[__cxx1::when_spelled(name)]]`, this compiler's own attribute for
// its headers, has a declaration read only in a unit that spells the name after it.
// Seen on the way and left for the next release: `X<void>::f()` is refused where f is a static data member that
// is a pointer to function.
// The suite's p176: sq01, as01, al01, st01, sm01, th01, so01, lk01, ws01, ex01.
//
// 1.77 (2026-11-20): the gaps 1.76 left, and what filling them showed. A probe of `noexcept` beside cl - five
// hundred questions of the language and the library - was the measure; it began as one line of EXCLUSIONS.md.
// Wrong code, silent: `static_cast<K>(k)` and `(K)k` of an object to its own class were the object itself - no
// copy constructor ran, and `static_cast<K>(k).set(7)` wrote into k ([expr.static.cast]/4); the same cast of a
// derived object to a base by value, read through `.`, ended in the code generator. A call written in a
// namespace that has a function of the name never reached the global namespace's function of a global class
// (`swp(c, c)` inside `lib` with a `lib::swp`), and a template's call missed the function of the argument's
// namespace declared after the template - so `n::swap(E &, E &)`, declared after <utility>, was not what
// std::swap of an array of E, pair<E, C>::swap or iter_swap called ([basic.lookup.argdep], [temp.dep.candidate]).
// Refused where valid: `X<void>::f()` through a static data member that is a pointer to function; `delete (T *)p`
// ([expr.delete]/1, a cast-expression); `dynamic_cast` to the operand's own class or a base of a class with no
// virtual function ([expr.dynamic.cast]/3, /5); `a = b` of a class that writes both assignments `= default`;
// `noexcept(noexcept(x.swap(y)))` and every exception-specification that names the function's parameters
// ([basic.scope.block]/2).
// What `noexcept(e)` and the traits answered wrong: is_nothrow_destructible said false of every class with a
// destructor of its own ([class.dtor]/3 makes it noexcept), and with it is_nothrow_move_constructible of
// unique_ptr and of everything holding one; the constructor an implicit conversion calls, the copy that
// initialises a by-value parameter and the copy a cast makes were not counted (is_nothrow_assignable<C &, int>,
// the last line of 1.75's list); what a `sizeof` or `decltype` inside the operand names was counted; a call of a
// virtual function was "through a pointer" whatever the function promised; a member template's specialization
// had no exception-specification at all, so `shared_ptr<B>(d)` could throw; a move assignment written
// `= default` had none either, and `noexcept = default` was not noexcept.
// What it cost in code: a `noexcept` specialization of a function template carried a handler calling
// std::terminate whatever its body - std::move's too - because the count of calls was not started at the body;
// and a specialization named only in an unevaluated operand was written into the program ([basic.def.odr]/2),
// so what it called had to be defined for the link.
// In the headers, what C++11 writes noexcept and was not: the observers of map, multimap, set, multiset and
// array, initializer_list, move, forward, swap and pair's and tuple's swap (as the standard conditions them),
// tuple's copy, move and move assignment, the constructors of exception, bad_alloc, bad_cast and bad_typeid,
// terminate, type_info's members, duration's default constructor; and the leaf helpers a destructor calls
// (the tree's walk, char_traits, allocator::deallocate, the string's internals), vector's move constructor.
// `what()` is left without it: a program's override written without noexcept, which cl takes, would be refused.
// Measured on the C6747 with CCS's SampleExt: 4.8 KB less unoptimized, 2.6 KB less at -O2, the output the same.
// Left, in EXCLUSIONS.md: a member declared after it named in a member's exception-specification (CWG 1330);
// `noexcept(e)` on a variadic function template; a copy constructor [class.copy]/7 deletes for a written move
// assignment is still called; `T t(e);` where a template's T is a reference; and - no fault, a cost - a temporary
// copied into the object it initialises where [class.copy]/31 lets the copy be left out, as cl and clang leave it.
// An unevaluated operand puts back the marks of use it made, each its own (a mark is what has a specialization's
// body written): 0.3 s more on the suite's heaviest program, 1.3 s to 1.6 s.
// The suite's p177: dm01, dc01, nx01, df01, cc01, ad01, ns01, ns02, lb01.
//
// 1.78 (2026-11-20): the copies [class.copy]/31 lets an implementation leave out, left out - the cost 1.77's
// probe measured and listed. A temporary of a class that would be copied or moved into an object of that class
// is that object: a local (`K h = K(5);`, `K h(K(5));` - the variable takes the temporary's frame slot), a
// by-value argument (`take(K(5))` - the temporary's address is what is passed; who destroys it stays as the ABI
// has it for a copy), the returned object of a class with no destructor (`return M(a);` called M's move; one
// with a destructor was built in place already), a temporary's own argument (`K(K(5))`), and - the temporary's
// constructor given the object's address - an object at namespace scope, a static member, a static local, an
// array's element, a member named in an initialiser list (`k(K(a))`), the object of `new K(K(6))`. The
// constructor left out is still the one that is chosen and checked: accessible, and not explicit in a
// copy-initialisation. What is not left out: an arm of `?:` (cl copies one there too), and a derived temporary
// into a base. With it a program that counts its constructors prints cl's and clang's numbers.
// Refused where valid, met on the way: `K g(K(42));` at namespace scope was read as a function's declaration
// ([dcl.ambig.res]/1: `K(42)` is no parameter).
// Measured on the C6747 at -O2: 0.6 % less code over the suite's seventeen sweep programs, 3.6 % in the one of
// tuples and pairs; none in CCS's SampleExt, which writes no such initialisation.
// The suite's p178: ce01, ce02, dg01.
//
// 1.79 (2026-11-20): three of 1.77's listed gaps, and what three sweeps of the language beside cl found - lambdas,
// classes, templates with the rest of C++11, each a program built by cl and by cpp11 and the outputs compared.
// Refused where valid: a reference initialised in parentheses, `int &r(v);` - in a block, as a static local, at
// namespace scope, and as `T t(e);` where a template's T is a reference ([dcl.init]/16); a reference to a pointer
// given an array or a function, `void f(const char *const &); f("s");`, and with it `std::tuple<int, const char
// *> t(1, "s");` and make_tuple of a string literal ([dcl.init.ref]/5 with [conv.array]); a lambda with an
// exception-specification, `[]() noexcept { }` ([expr.prim.lambda]/5); a pointer to a member of the class being
// defined, written inside it, on Windows (`int Outer::*pick();` - a member function pointer made there takes
// cl's shape for the class's model once the class is complete); a member function whose parameter list is
// inside its declarator's parentheses - `int (*f())(int);`, `int (C::*g())();`, `int (&h())[3];` - which was a
// data member, or was declared with no parameters ([dcl.fct]); a template template parameter's pack deduced
// from a template that lists its parameters one by one, `C<T...>` against `std::vector<int>`
// ([temp.deduct.type]/9).
// Accepted where ill-formed: the copy constructor of a class that writes a move assignment is deleted
// ([class.copy]/7) - such a class was copied by its bytes; three programs of the suite now refused.
// Answered wrong: `noexcept(e)` on a variadic function template was never read - a pack, or a non-type
// parameter, made the specification wait - so `noexcept(std::swap(t, t))` of two tuples said false.
// Left, in EXCLUSIONS.md: `c + 3` where c's class has both `operator+` and a conversion to double is ambiguous
// and is taken as the member's; an attribute on a declaration it does not apply to is not refused.
// The suite's p179: rf01, pb01, nv01, mp01, tt01; neg dc01, dc02, dc03.
//
// 1.80 (2026-11-20): the three gaps 1.79 listed, and what four more sweeps beside cl and clang found - expressions,
// conversions and overload resolution; declarations and initialisation; exceptions and lifetimes; the library.
// Refused where valid: a template-argument list with an operator in parentheses inside it, as an argument of a
// call - `f(std::is_same<decltype(a < b), bool>::value)`, `X<(n >> 1)>` (fifteen scans counted the angles
// inside the parentheses; groupCloseAt); an assignment or a prefix `++` assigned to again - `(a = b) = c`,
// `(a += 1) += 2`, `++ ++a`, `(++a)++` - and `&(a = b)`, which the code generator refused ([expr.ass]/1); unary
// `-`, `~`, `*` and a subscript on a class with a conversion function to a number or a pointer, `p == nullptr` for
// such a class, and of two conversion functions the one the object chooses ([over.built], [over.match.conv]/1);
// `&*pf` and `(&*pf)(2)` for a pointer to a function, which was `&pf`; `enum E : bool` ([dcl.enum]/2);
// `const int &r = {9};` in a block, and with or without `=` at namespace scope and as a static
// ([dcl.init.list]/3); a braced element of an initializer_list whose class has an initializer-list constructor
// the items do not fit, `std::vector<S> v{{"x", "y"}, {'a'}}` ([over.match.list]/1); a function type written
// after a pointer operator, `const char *()`, `int *(int)` ([dcl.name]); a conversion to a member class of a
// class template's specialization through its constructor, where being a parameter's type had not defined the
// class ([temp.inst]/1); a member's `noexcept(e)` that names a member declared below it (CWG 1330 - read again
// when the class is complete, round by round until nothing changes), and there and in a trailing return type a
// non-static member function called by its bare name or through `this` ([expr.prim.general]/3).
// In <string>: `s.erase(0)`, `s.erase(0, 0)`, `s.replace(0, 0, t)`, `s.insert(0, 0, c)` were ambiguous - the
// iterators are pointers and 0 is a null pointer constant; the iterator overloads that have a position overload
// beside them take a `__where`, which a pointer converts to.
// Answered wrong: `f(e)` for `enum E : short` chose f(int) over f(short), and f(short) beside f(long) was
// ambiguous ([conv.prom]/4, CWG 1601); `static_cast<D>(s)` and `(D)s` called s's conversion function where D
// has a constructor from it - a cast from another class is `D t(s);`; `P(v)` for a class P with no constructor
// and a v whose class converts to P copied v's bytes as if they were a P; `decltype(S().*pm)` was `int` and is
// `int &&` (CWG 1213); a member's `noexcept(e)` that calls a member declared below it answered its early
// reading. Permitted and now as cl: `new T[n]{T(1), T(2)}` builds each element in place.
// Accepted where ill-formed: `c + 3` where the class's own operator and the built-in one through its
// conversion function are each the better for one operand ([over.match.oper]/3); `D d = s;`, `const D &r = s;`,
// an array's or an aggregate's element from s, where D's constructor from S and S's conversion function to D
// are equally good ([over.match.copy]/1 - an argument and a returned value were refused already);
// `[[noreturn]]` and `[[carries_dependency]]` on a class or an enumeration.
// Left, in EXCLUSIONS.md: a member's noexcept asked inside its class before the class is complete; `this->m`
// of a data member in a trailing return type in the class body; on Windows, a local aligned beyond 16 bytes in
// a function with a try or a destructor.
// The suite's p180: ta01, as01, bo01, fd01, ep01, dc01, rb01, nl01, sz01, na01, ne01, op01; neg am01-am04,
// at01, at02.
//
// 1.81 (2026-11-24): the wrong code the user trial of Setup 1.10.5 found - three users, 73 programs of their own,
// every fault reduced and judged by clang. Answered wrong, silently: an object of a class whose copy is a call,
// returned by value, was built in the callee's frame and its bytes moved to the caller's - an object holding its
// own address pointed back into the dead frame, and every std::map, std::set and std::list returned by value
// walked on past its end ([class.copy]/31-32: a temporary of the class is now built at the caller's address, the
// named return value is the caller's object where every `return` of its scope names it, and anything else is
// moved or copied there by its constructor - returnInPlace); a virtual call's class result was copied by its
// bytes into another temporary and destroyed under the copy (`(.vc = &obj, call)` was not seen as a call -
// resultCall); `return make(x).b;` destroyed the temporary before copying its member; a temporary in an arm of
// `?:` whose result is not a class was destroyed whichever arm ran ([expr.cond]/1); the temporaries of a member's
// default construction in a constructor - `allocator_type()` for an unordered_map - were left pending and
// destroyed in the next inline member function, on its frame; `const B<T> *` was not deduced from a pointer
// variable to a derived class, only from `&x` ([temp.deduct.call]/2), and an overload taking `...` was chosen;
// an enumeration inside a function type was keyed as its underlying type, so `Name<void(Color)>` was
// `Name<void(int)>` ([temp.type]/1). Missing code: an explicit instantiation definition of a class or a member
// emitted nothing nobody called in the unit ([temp.explicit]/8); a constexpr function was not inline
// ([dcl.constexpr]/2), and one in a header two units include was "already defined".
// Listed in EXCLUSIONS.md: the 29 shapes of valid C++11 the trial found refused, a crash, three of the library.
// The suite's p181: rv01, nr01, vc01, co01, mt01, um01, dp01, ef01, ei01 (two units), ci01 (two units).
//
// 1.82 (2026-11-25): the 29 shapes of valid C++11 the user trial of Setup 1.10.5 found refused, and its crash.
// The crash: a typedef gave an unnamed class its name for linkage wherever one named it, not only where its
// declaration defines the class ([dcl.typedef]/9) - `typedef T type;` in a template over an unnamed union, and the
// union's name held a specialization over itself: std::swap of one recursed until the stack ran out. Lookup: a
// friend's body written in its class finds the class's statics ([class.friend]/7, friendScope_); a lambda's `[&]`
// keeps a const object const, its operator() in a member template is no member template's, its parameters may
// expand a pack; `name<...>` in a class's body is its member template before a namespace's; a member template of a
// global class reads in the global namespace wherever asked for, and argument-dependent lookup reaches a namespace's
// function template beside an ordinary function found first; a function template's specialization keeps its own
// namespace; `la::rank(x)` beside another namespace's class template rank; a using-directive's names join the
// global namespace's ([namespace.udir]/2); `ns::S::S()`'s mem-initializers read in ns; a static member's default
// argument sees the class template's parameters. Templates: a member template's declarator in parentheses
// (`(*f())(void *)`); `&C::f<T>` of a static member template; a nested type in an out-of-line member's parameters;
// `Hook T::*M` read in its template's namespace; a partial specialization leaving defaults out; an array bound's
// parentheses in an out-of-line static member; `X{r}` for the injected-class-name; a typedef of a specialization
// over an incomplete class waits for a use ([temp.inst]/1); `const K` and `K` were one key spelling, and
// `std::map<K, V> &` deduced K twice; a pack pattern's end counted a member template's `>` against parentheses;
// `sizeof...` in a signature (`sZ`); zero-initialising an enum class. constexpr: a list temporary folds; a
// constexpr friend or operator returning its class keeps no const; a call through a function-pointer argument.
// Special members: a defaulted copy assignment beside a written move assignment. A friend function template
// defined in its class ([temp.friend]). Found when the trial's projects were built again: a call named by a
// block's `using std::cos;` is unqualified for argument-dependent lookup, which makes the associated namespaces'
// function templates (ad::cos<T> from ad::sin<T>), and such a using-declaration hides the namespace's own
// templates of the name; a typedef puts off its own template-id alone, not what its arguments make.
// The suite's p182: fr01, un01, la01, mt01, nl01, tp01, cx01.
//
// 1.83 (2026-11-25): the library faults the user trial of Setup 1.10.5 found, and one its case found beside
// them. A namespace's class template named bare in an expression where nothing brings it is refused as in a
// type ([basic.lookup.unqual]; `is_same<int, int>::value` at global scope was std's), and a function template a
// call can reach is not hidden by such a class template (a program's own `rank` beside std::rank). On the C6747,
// math.h's `trunc` and `round` are inline functions in C++ and macros for RTS6x's names in C alone ([headers]/6:
// <sstream>, <cmath>, <iostream> in that order failed in <fstream>, ios_base::trunc taken by the macro), and
// numeric_limits says what CCS 7.4's <limits> says - denorm_absent, denorm_min() the least normal value, not IEC
// 559 - the C674x flushing subnormals as sim6747 does. And fpclassify and isnormal of a float judge it as a
// float, not widened to a double, where every subnormal float was normal. The suite's p183: tt01, ho01, dn01; neg
// tn01.
#define CXX1_VERSION      "1.83"
#define CXX1_SEAL_DATE    "25-11-2026"
#define CXX1_SEAL_FILE    "cxx1-1.83.dat"

// The line every run prints first; what it claims is docs/EXCLUSIONS.md's to measure (review of 2026-10-08).
#define CXX1_BANNER "©2026 G. R. Akhtar - C++03 with C++11 extensions"

// What --version answers, in the one shape every program of the toolchain uses (2026-11-12), so that an IDE's About
// reads them all alike: `name x.y - what it is`, then the copyright on a line of its own. cpp11's seal and build
// time follow on theirs.
#define CXX1_WHAT  "C++03 with C++11 extensions"
#define CXX1_OWNER "\xC2\xA9" "2026 G. R. Akhtar"

#endif
