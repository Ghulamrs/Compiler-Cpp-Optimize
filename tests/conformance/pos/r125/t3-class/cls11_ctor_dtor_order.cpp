// [class.base.init]/10, [class.dtor]: construction order bases (virtual first) then members in declaration order; destruction reverse; arrays; temporaries destroyed at full-expression end.
#include <cstdio>
struct T { const char *n; T(const char *n) : n(n) { std::printf("+%s ", n); } ~T() { std::printf("-%s ", n); } };
struct VBase { T t; VBase() : t("vb") {} };
struct B1 : virtual VBase { T t; B1() : t("b1") {} };
struct B2 : virtual VBase { T t; B2() : t("b2") {} };
struct D : B1, B2 { T m1, m2; D() : m2("m2"), m1("m1") { std::printf("D-body "); } ~D() { std::printf("D-dtor "); } };
T temp(const char *n) { return T(n); }
int main() {
    { D d; std::printf("| "); }
    std::printf("\n");
    { T arr[3] = {T("a0"), T("a1"), T("a2")}; std::printf("| "); }
    std::printf("\n");
    std::printf("%d ", (int)sizeof(temp("tmp"))); temp("t1"), std::printf("mid "); std::printf("end\n");
    { const T &r = temp("ext"); std::printf("held "); }
    std::printf("\n");
    return 0;
}
