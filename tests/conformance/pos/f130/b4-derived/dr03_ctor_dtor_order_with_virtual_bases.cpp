#include <cstdio>
struct V { V(int x = 0) { std::printf("V%d ", x); } ~V() { std::printf("~V "); } }; struct A : virtual V { A() : V(1) { std::printf("A "); } ~A() { std::printf("~A "); } }; struct B : virtual V { B() : V(2) { std::printf("B "); } ~B() { std::printf("~B "); } }; struct M { M() { std::printf("M "); } ~M() { std::printf("~M "); } };
struct D : A, B { M m; D() : V(3), B(), A() { std::printf("D "); } ~D() { std::printf("~D "); } };
int main() { { D d; } std::printf("\n"); return 0; }
