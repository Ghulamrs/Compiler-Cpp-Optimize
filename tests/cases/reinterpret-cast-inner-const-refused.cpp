// [expr.const.cast]/8: the first shared level is 'const char *const' against
// 'const char *', so a const is taken off one level down. clang refuses it.
const char **strip(const char *const *p) { return reinterpret_cast<const char **>(p); }
int main() { return 0; }
