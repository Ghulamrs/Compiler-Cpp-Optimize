#include <cstdio>
struct E { static int alive; int id; E(int id) : id(id) { if (id == 2) throw id; alive++; } ~E() { alive--; } }; int E::alive = 0;
int main() { try { E arr[3] = {E(0), E(1), E(2)}; } catch (int e) { std::printf("c%d %d ", e, E::alive); } try { E *p = new E(2); (void)p; } catch (int e) { std::printf("c%d %d", e, E::alive); } std::printf("\n"); return 0; }
