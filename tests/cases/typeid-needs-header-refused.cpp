// `typeid` yields a std::type_info, and without a declaration of it there is
// nothing for the expression to be.
int main() { return sizeof(typeid(int)); }
