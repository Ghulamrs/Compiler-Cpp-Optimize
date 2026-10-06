// A static reference member bound to a temporary is refused, as a reference at namespace
// scope is: [class.temporary]/5 would give the temporary static storage of its own, which
// this compiler does not make yet. clang accepts this program.
struct S { static const int &cr; };
const int &S::cr = 7;
int main() { return S::cr; }
