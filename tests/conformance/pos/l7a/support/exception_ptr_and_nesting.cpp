#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
struct Plain { int code; };
struct Oops : std::runtime_error { Oops(const char *w) : std::runtime_error(w) {} };
std::exception_ptr capture(int which) {
    try {
        if (which == 0) throw std::logic_error("logic");
        if (which == 1) throw 42;
        Plain p = {7};
        throw p;
    } catch (...) {
        return std::current_exception();
    }
}
void inner() { throw std::runtime_error("inner failure"); }
void outer() {
    try {
        inner();
    } catch (...) {
        std::throw_with_nested(Oops("outer failure"));
    }
}
void report(const std::exception &e, int depth) {
    std::printf("%d: %s\n", depth, e.what());
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &nested) {
        report(nested, depth + 1);
    }
}
int main() {
    std::exception_ptr none;
    std::printf("%d %d %d\n", (int)(bool)none, (int)(none == nullptr), (int)(std::current_exception() == nullptr));
    for (int i = 0; i < 3; i++) {
        std::exception_ptr p = capture(i);
        std::exception_ptr q = p;
        std::printf("%d %d %d ", (int)(bool)p, (int)(p == q), (int)(p != none));
        try {
            std::rethrow_exception(q);
        } catch (const std::logic_error &e) {
            std::printf("logic_error %s\n", e.what());
        } catch (int n) {
            std::printf("int %d\n", n);
        } catch (const Plain &pl) {
            std::printf("Plain %d\n", pl.code);
        }
    }
    std::exception_ptr made = std::make_exception_ptr(std::string("made"));
    try {
        std::rethrow_exception(made);
    } catch (const std::string &s) {
        std::printf("string %s\n", s.c_str());
    }
    std::exception_ptr a = capture(1), b = capture(1);
    std::printf("%d\n", (int)(a == b));
    swap(a, none);
    std::printf("%d %d\n", (int)(bool)a, (int)(bool)none);
    none = nullptr;
    std::printf("%d\n", (int)(bool)none);
    try {
        outer();
    } catch (const std::exception &e) {
        report(e, 0);
        const std::nested_exception *n = dynamic_cast<const std::nested_exception *>(&e);
        std::printf("nested %d %d\n", (int)(n != 0), (int)(n != 0 && n->nested_ptr() != nullptr));
    }
    try {
        std::throw_with_nested(5);
    } catch (int n) {
        std::printf("plain throw %d\n", n);
    }
    std::rethrow_if_nested(5);
    std::runtime_error flat("flat");
    std::rethrow_if_nested(flat);
    std::printf("end\n");
    return 0;
}
