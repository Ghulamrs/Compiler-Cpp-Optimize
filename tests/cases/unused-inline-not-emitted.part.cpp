// The second translation unit of unused-inline-not-emitted.cpp: the same header, nothing of it used.
extern "C" int never_linked(int);
extern "C" int linked(int);
inline int wrapper(int x) { return never_linked(x); }
static inline int hidden(int x) { return never_linked(x) + 1; }
inline int used(int x) { return linked(x) + 1; }
struct Tool {
    int value;
    int unusedMember() const { return never_linked(value); }
    int usedMember() const { return linked(value); }
    static int unusedStatic(int x) { return never_linked(x); }
};
int partValue() { return 50; }
