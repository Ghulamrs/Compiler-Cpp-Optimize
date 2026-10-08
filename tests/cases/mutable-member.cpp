// **`mutable` on a data member**, which [dcl.stc]/9 makes writable through a
// const object - the keyword a class reaches for when something it caches or
// counts is not part of the value it presents. Five of Compiler++'s sixteen
// sources were stopped by one `mutable bool` in one header.
//
// What it took was three lines, because the propagation was already in one
// shape at each of the three places a member is reached: `.`, `->`, and the
// implicit `this->name` inside a member function each ask whether the object
// is const and qualify the member's type if it is. A mutable member is the
// second exception there, beside the reference member that was already
// exempt - and it is a property of the member rather than of its type, which
// is why the flag sits on `Member` and not in the qualifiers.
extern "C" int printf(const char *, ...);

struct Cache {
    int value;
    mutable int reads;
    mutable bool warned;
    // A const member function writes them through `this`, which is the third
    // of the three paths and the one that goes through no `.` or `->` at all.
    int get() const { reads++; if (!warned) warned = true; return value; }
};

int through(const Cache &c) { return c.get() + c.get(); }

int main() {
    Cache c; c.value = 21; c.reads = 0; c.warned = false;
    const Cache &r = c;
    int sum = through(c);
    printf("%d %d %d\n", sum, c.reads, (int)c.warned);
    // Through a pointer to const, and read back through a reference to const.
    const Cache *p = &c;
    p->reads = 100;
    printf("%d %d\n", r.reads, p->reads);
    return 0;
}
