// [class.access.nest]/1 (C++11): a nested class is a member and has access to the enclosing class's private members.
#include <cstdio>
class Outer { int secret; public: Outer() : secret(42) {} class Inner { public: int peek(const Outer &o) const { return o.secret; } }; };
int main() { Outer o; Outer::Inner i; std::printf("%d\n", i.peek(o)); return 0; }
