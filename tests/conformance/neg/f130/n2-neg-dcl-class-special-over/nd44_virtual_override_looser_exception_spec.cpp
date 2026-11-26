struct B { virtual void f() noexcept; }; struct D : B { void f() override; }; int main() { return 0; }
