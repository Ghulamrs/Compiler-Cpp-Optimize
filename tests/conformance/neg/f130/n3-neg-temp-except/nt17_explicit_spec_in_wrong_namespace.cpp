namespace N { template <class T> void f(T); } namespace M { template <> void N::f(int); } int main() { return 0; }
