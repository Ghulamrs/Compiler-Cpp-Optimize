#include <cstdio>
struct T { static int next; int id; T() : id(next++) { std::printf("+%d ", id); } ~T() { std::printf("-%d ", id); } }; int T::next = 0;
struct H { T a[3]; T b; };
int main() { { H h; T arr[2]; } std::printf("\n"); return 0; }
