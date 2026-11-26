struct T { ~T() noexcept(false) {} };
static_assert(!noexcept(T()), "dtor");
static_assert(!noexcept(new int), "new");
static_assert(!noexcept(throw 1), "throw");
int main() { return 0; }
