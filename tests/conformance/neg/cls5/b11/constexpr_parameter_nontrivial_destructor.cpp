// [dcl.constexpr]/3: a constexpr function's parameter type is a literal type, and a class with a
// user-provided destructor is not one.
struct Handle {
    int v;
    ~Handle() {}
};
constexpr int value(Handle h) { return h.v; }
int main() {
    return 0;
}
