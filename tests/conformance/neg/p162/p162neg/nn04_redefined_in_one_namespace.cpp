// [basic.def.odr]: two definitions of one template in one namespace.
namespace a { template <class T> struct X { T v; }; }
namespace b { template <class T> struct X { T v; }; }
namespace a { template <class T> struct X { T w; }; }
int main() { return 0; }
