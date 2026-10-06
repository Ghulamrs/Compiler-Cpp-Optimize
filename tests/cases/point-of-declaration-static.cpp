// [basic.scope.pdecl]/1 for a static local: in scope in its own initialiser, the
// constant one laid down in the image and the one with a constructor built under
// its guard. Apart from point-of-declaration.cpp for the static locals' names.
extern "C" int printf(const char *, ...);

struct Self {
    Self *me;
    Self(Self *p);
};
Self::Self(Self *p) : me(p) { }

int staticLocal() {
    static void *s = &s;
    static Self k(&k);
    static int z = sizeof(z);
    return s == &s && k.me == &k && z == 4;
}

int main() {
    printf("%d %d\n", staticLocal(), staticLocal());
    return 0;
}
