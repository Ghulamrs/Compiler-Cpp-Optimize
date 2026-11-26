// [namespace.qual]: k has no Box, whatever the namespace around it declares.
template <class T> struct Box { T v; };
namespace k { int x; }
k::Box<int> bad;
int main() { return 0; }
