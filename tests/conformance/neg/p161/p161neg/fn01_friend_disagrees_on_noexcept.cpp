// [except.spec]/3: every declaration of a function says the same exception specification.
void f();
struct S { friend void f() noexcept; };
int main() { return 0; }
