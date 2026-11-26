// [expr.const]/2: a constexpr function reading past a string literal's terminator.
constexpr char at(const char *s, int i) { return s[i]; }
static_assert(at("ab", 4) == 0, "past the end");
int main() {
    return 0;
}
