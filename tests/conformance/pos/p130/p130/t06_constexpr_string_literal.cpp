constexpr int len(const char *s) { return *s ? 1 + len(s + 1) : 0; }
static_assert(len("abc") == 3, "len");
int main() { return 0; }
