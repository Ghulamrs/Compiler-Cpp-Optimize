// [class.access]/1: a protected member class template is not named from an unrelated scope.
struct B { protected: template <class T> struct Prot { T p; }; };
int main() { B::Prot<int> p; p.p = 1; return p.p; }
