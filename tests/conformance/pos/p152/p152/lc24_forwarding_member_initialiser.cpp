#include <cstdio>
#include <string>
#include <utility>
struct Holder {
    std::string s;
    template <class U> explicit Holder(U &&u) : s(std::forward<U>(u)) {}
};
template <class T> struct Box {
    T first;
    template <class U> explicit Box(U &&u) : first(std::forward<U>(u)) {}
};
int main() {
    setvbuf(stdout, 0, _IONBF, 0);
    const char (&lit)[4] = "one";
    std::string direct(lit);
    std::printf("1 %s\n", direct.c_str());
    Holder h("two");
    std::printf("2 %s\n", h.s.c_str());
    Box<std::string> b("three");
    std::printf("3 %s\n", b.first.c_str());
    Box<const std::string> c("four");
    std::printf("4 %s\n", c.first.c_str());
}
