struct B { virtual void f() noexcept {} };
struct D : B { void f() throw(int) {} };
int main() { return 0; }
