// [class.base.init]/8: a reference member a written constructor's list leaves
// out is ill-formed - a reference is bound in the list or never. clang:
// "constructor for 'S' must explicitly initialize the reference member 'r'".
// The refusal is at the constructor; `: r()` has its own message.
int g = 4;

struct S {
    int &r;
    int n;
    S() : n(1) {}
};

int main(void) {
    S s;
    return s.n;
}
