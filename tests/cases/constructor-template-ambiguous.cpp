// Two constructor templates that deduce alike for an lvalue, [over.ics.rank]: `S(T)` and
// `S(const T &)` are both the identity conversion, and clang refuses the call as ambiguous.
struct S {
    int x;
    template <class T> S(T t) : x((int)t) {}
    template <class T> S(const T &t) : x((int)t + 1) {}
};
int main() {
    int n = 3;
    S s(n);
    return s.x;
}
