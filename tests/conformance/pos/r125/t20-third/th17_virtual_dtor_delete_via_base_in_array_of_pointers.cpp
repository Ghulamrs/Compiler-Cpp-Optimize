// Polymorphic deletion through base pointers held in an array; destructor order and counts.
#include <cstdio>
struct B { static int alive; B() { ++alive; } virtual ~B() { --alive; } virtual int k() const { return 0; } };
struct D1 : B { int k() const override { return 1; } ~D1() { std::printf("~D1 "); } };
struct D2 : B { int k() const override { return 2; } ~D2() { std::printf("~D2 "); } };
int B::alive = 0;
int main() { B *arr[3] = {new D1, new D2, new D1}; int s = 0; for (B *p : arr) s += p->k(); std::printf("%d %d ", s, B::alive); for (B *p : arr) delete p; std::printf("%d\n", B::alive); return 0; }
