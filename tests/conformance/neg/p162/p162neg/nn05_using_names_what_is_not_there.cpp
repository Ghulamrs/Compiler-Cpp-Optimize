// [namespace.udecl]/1: the name must be a member of the namespace named.
template <class T> struct Box { T v; };
namespace k { int x; }
using k::Box;
int main() { return 0; }
