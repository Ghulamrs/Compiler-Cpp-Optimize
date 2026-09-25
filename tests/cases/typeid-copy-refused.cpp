// std::type_info is neither copied nor made: its copy constructor is private.
#include <typeinfo>
int main() { std::type_info t = typeid(int); return 0; }
