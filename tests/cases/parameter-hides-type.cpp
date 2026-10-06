// [basic.scope.hiding]/2: in `void f(tm *tm)` the parameter hides the class in the body, so
// `tm->tm_sec` is a member access, `sizeof(tm)` is a pointer's size and `tm == 0` a compare;
// `struct tm` still names the type there, and a name before `::` still looks at types only.
extern "C" int printf(const char *, ...);

struct tm {
    int tm_sec;
    int tm_min;
    static int count;
};
int tm::count = 40;

int minutes(const tm *p) { return p->tm_min; }

void set(tm *tm) {
    tm->tm_sec = 11;
    tm[0].tm_min = 12;
}

int read(const tm *tm) {
    int x = tm->tm_sec;
    struct tm y = *tm;
    int big = sizeof(tm) == sizeof(void *);
    return x + big + (tm == 0) + minutes(tm) + y.tm_sec + tm::count;
}

int local() {
    struct tm v;
    v.tm_sec = 3;
    v.tm_min = 4;
    int tm = v.tm_sec + v.tm_min;
    tm *= 2;
    return tm;
}

int main() {
    struct tm t;
    set(&t);
    printf("%d %d\n", t.tm_sec, t.tm_min);
    printf("%d\n", read(&t));
    printf("%d\n", local());
    return 0;
}
