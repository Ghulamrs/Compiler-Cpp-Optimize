#include <cstdio>
int main() {
    std::printf("%d %d %d %d %d\n", (int)sizeof(u8"a" "b"), (int)sizeof("a" u8"bc"),
                (int)sizeof(L"a" "b"), (int)sizeof(u"a" u"b" "c"), (int)sizeof(U"x" "y"));
    return 0;
}
