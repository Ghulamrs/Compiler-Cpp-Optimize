// [dcl.link]/6: the two declarations name one function, and they disagree about its type.
namespace a { extern "C" int q(int); }
namespace b { extern "C" double q(int); }
int main() { return 0; }