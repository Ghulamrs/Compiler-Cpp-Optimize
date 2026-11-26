// [class.cdtor]/4: a virtual call while a base is built or destroyed reaches that base's own function, in each base's table
#include <cstdio>
struct I1 { I1() { std::printf("I1:%d ", who()); } virtual ~I1() { std::printf("~I1:%d ", who()); } virtual int who() { return 1; } };
struct I2 { I2() { std::printf("I2:%d ", id()); } virtual ~I2() { std::printf("~I2:%d ", id()); } virtual int id() { return 2; } };
struct X : I1, I2 { X() { std::printf("X:%d:%d ", who(), id()); } ~X() { std::printf("~X:%d:%d ", who(), id()); } int who() { return 10; } int id() { return 20; } };
int main() { { X x; std::printf("| "); } std::printf("\n"); return 0; }
