// [class.dtor]/3 with [except.spec]/3: a destructor's definition says what its declaration said.
struct A { ~A() noexcept(false); };
A::~A() noexcept(true) {}
int main() { return 0; }
