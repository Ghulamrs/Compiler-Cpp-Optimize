// `template <class T> T Tm<T>::st;` is the definition of the static data
// member, [temp.static], not a redeclaration (the cl review's A14).
extern "C" int printf(const char *, ...);
template <class T> struct Tm { static T st; };
template <class T> T Tm<T>::st;
struct P { int a; };
int main() {
    typedef Tm<int> TI; typedef Tm<P> TP;
    TI::st = 4; TP::st.a = 5;
    printf("%d %d\n", TI::st, TP::st.a);
    return 0;
}
