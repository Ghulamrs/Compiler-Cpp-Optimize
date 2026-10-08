// A user-defined literal, [lex.ext] and [over.literal]: the suffix names a
// literal operator looked up at every literal that carries it, and the raw
// form wants a literal operator template. Neither path exists here, so the
// declaration is refused by name. clang -std=c++11 -pedantic-errors accepts
// this program and it returns 0.
long double operator"" _km(long double v) { return v * 1000.0L; }
int main() { return (int)(1.5_km) == 1500 ? 0 : 1; }
