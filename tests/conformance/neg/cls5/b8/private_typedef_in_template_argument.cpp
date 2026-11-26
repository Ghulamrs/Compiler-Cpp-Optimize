// [class.access]/1: a private typedef named in a template argument inside main.
template <class T> struct Box { T v; };
class S {
    typedef double T;
};
int main() {
    Box<S::T> b = { 1.5 };
    return static_cast<int>(b.v);
}
