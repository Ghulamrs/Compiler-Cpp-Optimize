// [namespace.udir]/6: both using-directives find a template of the name, and the use is ambiguous.
namespace a { template <class T> struct X { T v; }; }
namespace b { template <class T> struct X { T v; }; }
using namespace a;
using namespace b;
X<int> bad;
int main() { return 0; }
