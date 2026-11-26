template <class, class> struct Two {}; template <template <class> class TT> struct A {}; int main() { A<Two> a; return 0; }
