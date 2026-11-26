// [temp.expl.spec]/2: v1 is not inline, so lib declares no template ver for this to specialize.
namespace lib { namespace v1 { template <class T> int ver(T) { return 1; } } }
namespace lib { template <> int ver(int) { return 2; } }
int main() { return lib::v1::ver(1); }
