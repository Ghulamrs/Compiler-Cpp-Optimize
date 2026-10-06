// [expr.const.cast]/8: 'char **' to 'const char **' adds a const beneath a level with
// none - no qualification conversion does it - so it casts away constness. clang refuses it.
const char **widen(char **p) { return reinterpret_cast<const char **>(p); }
int main() { return 0; }
