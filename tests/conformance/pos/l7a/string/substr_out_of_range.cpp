#include <cstdio>
#include <stdexcept>
#include <string>
static void attempt(const char *what, const std::string &s, std::string::size_type pos, std::string::size_type n) {
    try {
        std::string r = s.substr(pos, n);
        std::printf("%s: [%s]\n", what, r.c_str());
    } catch (std::out_of_range &) {
        std::printf("%s: out_of_range\n", what);
    }
}
int main() {
    std::string s = "abc";
    attempt("substr(0)", s, 0, std::string::npos);
    attempt("substr(2, 5)", s, 2, 5);
    attempt("substr(3)", s, 3, std::string::npos);
    attempt("substr(3, 0)", s, 3, 0);
    attempt("substr(4)", s, 4, std::string::npos);
    attempt("substr(4, 0)", s, 4, 0);
    attempt("substr(npos)", s, std::string::npos, 1);
    try { s.substr(5); std::printf("one-argument: no\n"); } catch (std::out_of_range &) { std::printf("one-argument: out_of_range\n"); }
    try { int r = s.compare(4, 1, "a"); std::printf("compare: %d\n", r); } catch (std::out_of_range &) { std::printf("compare: out_of_range\n"); }
    try { int r = s.compare(0, 1, std::string("xa"), 3, 1); std::printf("compare2: %d\n", r); } catch (std::out_of_range &) { std::printf("compare2: out_of_range\n"); }
    try { s.substr(9); } catch (std::logic_error &) { std::printf("as logic_error\n"); }
    return 0;
}
