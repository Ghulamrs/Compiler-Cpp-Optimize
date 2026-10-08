// `__COUNTER__` - an extension clang, g++ and cl share, not C++11: it expands to
// 0, 1, 2 ... in the order the expansions happen, once per expansion, in a
// macro's body, a macro's argument and an `#if` alike. `defined` and `#ifdef`
// answer yes for it, and for `__LINE__` and `__FILE__`, which no `#define` wrote.
extern "C" int printf(const char *, ...);

#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define UNIQUE(base) CAT(base, __COUNTER__)
#define TWICE(x) ((x) * 10 + (x))

int UNIQUE(slot) = 7;
int UNIQUE(slot) = 8;

#if __COUNTER__ == 2
int fromIf = 1;
#else
int fromIf = 0;
#endif

#if defined(__COUNTER__) && defined(__LINE__) && defined(__FILE__)
int allDefined = 1;
#else
int allDefined = 0;
#endif
#ifdef __COUNTER__
int ifdefOk = 1;
#else
int ifdefOk = 0;
#endif

int main(void) {
    printf("%d %d\n", slot0, slot1);
    printf("%d %d %d\n", fromIf, allDefined, ifdefOk);
    int a = __COUNTER__;
    int b = TWICE(__COUNTER__);
    int c = __COUNTER__;
    printf("%d %d %d\n", a, b, c);
    return 0;
}
