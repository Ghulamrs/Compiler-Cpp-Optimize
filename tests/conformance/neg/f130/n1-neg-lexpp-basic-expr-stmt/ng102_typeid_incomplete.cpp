#include <typeinfo>
struct S; int main() { return typeid(S) == typeid(int); }
