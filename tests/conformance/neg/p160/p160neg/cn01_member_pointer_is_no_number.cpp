// [conv.bool] takes a pointer to member to bool and to nothing else.
struct S { int m; };
int main() {
    int S::*p = &S::m;
    int n = p;
    return n;
}
