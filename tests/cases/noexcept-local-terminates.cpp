// The half noexcept-terminates-callee.cpp leaves alone on the hosts: a
// `noexcept` function with a destructible local, whose callee throws. On the
// C6000 the function's table ends with TI's exception-specification row over
// the whole function, allowing nothing, so the runtime runs the local's
// destructor and then calls unexpected(), which ends the program - the output
// stops at "-1 ". The Itanium hosts wrap only a body with no pads of its own
// and let this one propagate; the case excuses them by name.
extern "C" int printf(const char *, ...);
extern "C" int fflush(void *);
extern "C" int close(int);

void thrower() { throw 2; }
struct R { int n; R(int v) : n(v) { printf("+%d ", v); fflush(0); } ~R() { printf("-%d ", n); fflush(0); } };
int withLocal() noexcept { R r(1); thrower(); return r.n; }

int main() {
    close(2);
    try { withLocal(); } catch (int e) { printf("caught %d - and should not have\n", e); }
    printf("returned - and should not have\n");
    return 0;
}
