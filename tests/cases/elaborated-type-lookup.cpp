// [basic.lookup.elab]/2: `struct tm *` inside a namespace or a class finds the global tm by
// ordinary lookup; only a name found nowhere is declared, and then in the nearest enclosing
// namespace, never in the class. The members and functions here link only if every spelling
// names the one type, so a second `tm` would be an undefined symbol or a mismatch.
extern "C" int printf(const char *, ...);

struct tm { int tm_sec; int tm_min; };

namespace rts6x {
class Calendar {
public:
    static void breakDown(long t, struct tm *out);
    static void two(tm *out);
    void inl(struct tm *out) { out->tm_min = 21; }
    void later(struct Later *p);
};
void reset(struct tm *out);
}

void rts6x::Calendar::breakDown(long t, struct tm *out) { out->tm_sec = (int)t; }

namespace rts6x {
void Calendar::two(struct tm *out) { out->tm_min = 2; }
void reset(struct tm *out) { out->tm_sec = 0; out->tm_min = 0; }
// Declared by the parameter above, in rts6x and not in Calendar.
struct Later { int v; };
void Calendar::later(Later *p) { p->v = 33; }
int peek(struct Later *p) { return p->v; }
}

int main() {
    struct tm x;
    rts6x::reset(&x);
    rts6x::Calendar::breakDown(7, &x);
    printf("%d %d\n", x.tm_sec, x.tm_min);
    rts6x::Calendar::two(&x);
    rts6x::Calendar c;
    printf("%d\n", x.tm_min);
    c.inl(&x);
    printf("%d\n", x.tm_min);
    rts6x::Later l;
    c.later(&l);
    printf("%d\n", rts6x::peek(&l));
    return 0;
}
