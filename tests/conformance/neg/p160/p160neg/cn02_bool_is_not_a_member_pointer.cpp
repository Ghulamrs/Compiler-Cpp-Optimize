// The conversion goes one way: bool to a pointer to member is no conversion.
struct S { int m; };
int main() {
    bool b = true;
    int S::*p = b;
    return p == nullptr;
}
