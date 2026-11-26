// Arrays of pointers to member functions; comparison of a member function pointer with 0 and nullptr.
#include <cstdio>
struct B { int vf() { return 1; } int nf() { return 2; } };
int main() {
    int (B::*fns[2])() = {&B::vf, &B::nf}; B b;
    int (B::*np)() = 0; int (B::*nq)() = nullptr;
    std::printf("%d %d %d %d %d\n", (b.*fns[0])(), (b.*fns[1])(), np == 0, nq == nullptr, fns[0] != 0);
    return 0;
}
