// [class.friend], [class.access]: friend functions and classes, friend of a nested, extended friend syntax (friend Cls;), access via protected in derived, access through using-declaration changing access.
#include <cstdio>
class Secret;
class Peeker;
class Secret { int v; friend class Peeker; friend int peek(const Secret &); friend Peeker; public: Secret() : v(7) {} protected: int prot() const { return v + 1; } };
int peek(const Secret &s) { return s.v; }
class Peeker { public: int get(const Secret &s) { return s.v * 2; } };
struct Der : Secret { int useProt() const { return prot(); } int viaDer() const { return prot(); } };
template <class T> class Tmpl { T t; friend int get(const Tmpl &x) { return x.t; } public: Tmpl(T t) : t(t) {} };
class Priv { int x; public: Priv() : x(5) {} private: int hidden() { return x; } public: int viaFriend(); friend int callHidden(Priv &p) { return p.hidden(); } };
int Priv::viaFriend() { return callHidden(*this); }
int main() {
    Secret s; Peeker p; Der d;
    std::printf("%d %d %d %d\n", peek(s), p.get(s), d.useProt(), d.viaDer());
    Tmpl<int> t(9); std::printf("%d\n", get(t));
    Priv pr; std::printf("%d\n", pr.viaFriend());
    return 0;
}
