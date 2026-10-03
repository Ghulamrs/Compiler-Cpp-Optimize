// Static construction on the C6747: file-scope objects in this unit are built before main, in the
// order written; a function-local static is built the first time control passes it; and the
// destructors run after main returns, in the reverse order of construction. The .init_array
// LNK6x was missing on 2026-10-03 is what makes the first part happen.
#include <stdio.h>

struct Tag {
    const char *name;
    int value;
    Tag(const char *n, int v) : name(n), value(v) { printf("construct %s %d\n", name, value); }
    ~Tag() { printf("destroy %s\n", name); }
};

static int counter = 0;
static int next() { return ++counter * 10; }

static Tag first("first", next());
static Tag second("second", next());
Tag third("third", first.value + second.value);

static int lazy() {
    static Tag once("once", 7);
    return once.value;
}

int main() {
    printf("main: %s %d %s %d %s %d\n", first.name, first.value, second.name, second.value, third.name, third.value);
    printf("lazy: %d\n", lazy());
    printf("lazy again: %d\n", lazy());
    printf("main returns\n");
    return 0;
}
