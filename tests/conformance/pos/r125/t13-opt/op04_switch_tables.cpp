// Switch lowering: dense tables, sparse cases, negative cases, 64-bit switch, char switch, switch inside loop with fallthrough and default, switch on enum with all values.
#include <cstdio>
const char *dense(int k) { switch (k) { case 0: return "a"; case 1: return "b"; case 2: return "c"; case 3: return "d"; case 4: return "e"; case 5: return "f"; case 6: return "g"; case 7: return "h"; default: return "?"; } }
int sparse(int k) { switch (k) { case -1000: return 1; case 0: return 2; case 1000: return 3; case 1000000: return 4; case 7: return 5; default: return 0; } }
int wide(long long k) { switch (k) { case 1LL << 40: return 1; case -(1LL << 40): return 2; case 0: return 3; default: return 4; } }
int chars(char c) { switch (c) { case 'a': case 'e': case 'i': case 'o': case 'u': return 1; case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9': return 2; default: return 0; } }
enum E { X, Y, Z };
int en(E e) { switch (e) { case X: return 10; case Y: return 20; case Z: return 30; } return -1; }
int main() {
    for (int k = -1; k < 9; ++k) std::printf("%s", dense(k)); std::printf("\n");
    int ks[] = {-1000, 0, 1000, 1000000, 7, 8, -1}; for (int k : ks) std::printf("%d", sparse(k)); std::printf("\n");
    std::printf("%d %d %d %d\n", wide(1LL << 40), wide(-(1LL << 40)), wide(0), wide(5));
    std::printf("%d %d %d\n", chars('e'), chars('5'), chars('x'));
    std::printf("%d %d %d %d\n", en(X), en(Y), en(Z), en((E)7));
    int acc = 0; for (int i = 0; i < 10; ++i) { switch (i % 4) { case 0: acc += 1; case 1: acc += 10; break; case 2: acc += 100; break; default: acc += 1000; } } std::printf("%d\n", acc);
    return 0;
}
