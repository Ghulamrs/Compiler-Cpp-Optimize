// A deleted virtual function is legal C++11 and refused by name here: its vtable slot
// would name a function with no body. clang accepts it.
struct B { virtual int f() = delete; virtual int g() { return 1; } };
int main() { return 0; }
