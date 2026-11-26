// [propagation]/7-10: an exception_ptr keeps the exception it refers to in being for as long as a pointer refers
// to it - past the handler that caught it - and rethrow_exception throws that exception again, as often as asked,
// also while it is being handled. Whether a copy is made is unspecified, so the objects in being are asked for as
// "some" and "none", never counted.
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>

static int alive;
struct Counted {
    int v;
    Counted(int v) : v(v) { alive++; }
    Counted(const Counted &o) : v(o.v) { alive++; }
    ~Counted() { alive--; }
};
struct Local { const char *n; Local(const char *n) : n(n) {} ~Local() { std::printf("~%s ", n); } };

std::exception_ptr capture(int v) {
    try { throw Counted(v); }
    catch (...) { return std::current_exception(); }
}

// the pointer outlives the handler, and the exception goes with the last pointer
void outlives() {
    std::exception_ptr p = capture(1);
    std::printf("after the handler: %s\n", alive > 0 ? "some" : "none");
    std::exception_ptr q = p;
    p = nullptr;
    std::printf("one pointer gone: %s\n", alive > 0 ? "some" : "none");
    try { std::rethrow_exception(q); } catch (const Counted &c) { std::printf("thrown again: %d\n", c.v); }
    q = std::exception_ptr();
    std::printf("both gone: %s\n", alive > 0 ? "some" : "none");
}

// one pointer thrown from three times, the objects between the throw and the handler destroyed each time
void again() {
    std::exception_ptr p = capture(2);
    for (int i = 0; i < 3; i++) {
        try { Local l("l"); std::rethrow_exception(p); }
        catch (const Counted &c) { std::printf("%d:%d ", i, c.v); }
    }
    std::printf("\n");
}

// thrown again while it is being handled, two and three deep
void nested() {
    std::exception_ptr p = capture(3);
    try { std::rethrow_exception(p); }
    catch (const Counted &a) {
        try { std::rethrow_exception(p); }
        catch (const Counted &b) {
            try { std::rethrow_exception(std::current_exception()); }
            catch (const Counted &c) { std::printf("three deep: %d %d %d\n", a.v, b.v, c.v); }
            std::printf("two deep: %d %d\n", a.v, b.v);
        }
        std::printf("one deep: %d\n", a.v);
    }
    p = nullptr;
    std::printf("after all three: %s\n", alive > 0 ? "some" : "none");
}

// throw; in the handler of an exception thrown from its pointer, and a pointer taken in the outer handler
void rethrown() {
    std::exception_ptr later;
    {
        std::exception_ptr p = capture(4);
        try {
            try { std::rethrow_exception(p); }
            catch (const Counted &c) { std::printf("inner %d ", c.v); throw; }
        } catch (...) {
            later = std::current_exception();
            std::printf("outer ");
        }
    }
    try { std::rethrow_exception(later); } catch (const Counted &c) { std::printf("later %d\n", c.v); }
}

// the last pointer goes while its exception is being handled: the handler still has it
void lastGoesInside() {
    std::exception_ptr p = capture(5);
    try { std::rethrow_exception(p); }
    catch (const Counted &c) {
        p = nullptr;
        std::printf("inside: %d %s\n", c.v, alive > 0 ? "some" : "none");
    }
    std::printf("outside: %s\n", alive > 0 ? "some" : "none");
}

// a pointer assigned to itself and swapped keeps its exception; copies compare equal
void copies() {
    std::exception_ptr p = capture(6), q;
    p = *&p;
    std::swap(q, p);
    std::exception_ptr r(q);
    std::printf("%d %d %d %d\n", (int)(bool)p, (int)(bool)q, (int)(q == r), (int)(p == r));
    try { std::rethrow_exception(r); } catch (const Counted &c) { std::printf("copy %d\n", c.v); }
}

// uncaught_exception is true while a rethrow_exception is on its way, false in its handler
struct Asks { ~Asks() { std::printf("on the way %d ", (int)std::uncaught_exception()); } };
void uncaught() {
    std::exception_ptr p = capture(7);
    try { Asks a; std::rethrow_exception(p); }
    catch (...) { std::printf("handled %d\n", (int)std::uncaught_exception()); }
}

// the dynamic type is kept: a derived class comes back as itself, through a pointer made of a base's handler
struct Deep : std::runtime_error {
    int extra;
    Deep(int e) : std::runtime_error("deep"), extra(e) {}
};
void dynamicType() {
    std::exception_ptr p;
    try { throw Deep(8); } catch (const std::exception &) { p = std::current_exception(); }
    try { std::rethrow_exception(p); }
    catch (const std::exception &e) {
        const Deep *d = dynamic_cast<const Deep *>(&e);
        std::printf("%s %d\n", e.what(), d ? d->extra : -1);
    }
    try { std::rethrow_exception(p); } catch (const Deep &d) { std::printf("as itself %d\n", d.extra); }
}

// a second exception thrown and handled while the first is kept, then the first thrown from its handler
void twoAtOnce() {
    std::exception_ptr first = capture(9);
    try { throw std::logic_error("second"); }
    catch (const std::logic_error &e) {
        try { std::rethrow_exception(first); }
        catch (const Counted &c) { std::printf("%d inside %s\n", c.v, e.what()); }
        std::printf("still %s\n", e.what());
    }
}

int main() {
    outlives();
    again();
    nested();
    rethrown();
    lastGoesInside();
    copies();
    uncaught();
    dynamicType();
    twoAtOnce();
    std::printf("at the end: %s\n", alive > 0 ? "some" : "none");
    std::printf("no exception: %d\n", (int)(std::current_exception() == nullptr));
    return 0;
}
