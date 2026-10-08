// A function template whose trailing return type is decltype of an expression
// over its parameters: Itanium spells such a signature with the expression
// itself, `_Z2thIiEDTmlfp_Li3EET_`, which this mangler cannot write - refused by
// name rather than named wrongly. clang compiles it; `-> T` works here.
template <class T> auto th(T a) -> decltype(a * 3) { return a * 3; }

int main(void) { return th(1) - 3; }
