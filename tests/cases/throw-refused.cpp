// **What `throw` still cannot name**, now that a class and a pointer can be
// thrown. A pointer to a function would want a `__function_type_info` for
// what it points at, which is not built; the Microsoft side has no
// descriptor for it either. throw-class.cpp and throw-pointer.cpp are the
// halves that work, and throw-multiple-bases-refused.cpp the other thing
// still refused.
void f() {}
int main() { try { throw &f; } catch (...) { return 1; } return 0; }
