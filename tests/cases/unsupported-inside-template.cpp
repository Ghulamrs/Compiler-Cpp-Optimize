// **A feature this compiler has not built is not a substitution failure.**
// Every diagnostic raised inside a `Trial` is thrown rather than printed, and
// the two template-instantiation sites caught them all - so a refusal reached
// while instantiating a candidate silently dropped that candidate, and the
// call then failed with a sentence that ended on "and": `why` was empty
// because deduction had *succeeded* and had no reason to give.
//
// [temp.deduct]/8 is about types and expressions that are ill-formed, not
// about what the compiler has implemented, so the two sites re-raise when the
// failure was a refusal. This template deduces perfectly well and its return
// type cannot be spelt in an Itanium name, and that is what the reader is told.
// (It was `-> A` until trailing return types landed; `-> decltype(a)` is the
// form still refused.)
//
// template-sfinae.cpp is the other half: a real substitution failure still
// drops its candidate and the program still runs.
template <class A> auto f(A a) -> decltype(a) { return a; }

int main() { return f(0); }
