// A using-declaration naming what another using-declaration put in a namespace: `using std::scanf;`
// where <cstdio> has `using ::scanf;` inside std. Both read an empty stdin, so both answer EOF.
#include <cstdio>
using std::scanf;
using std::fgets;
using std::printf;

namespace a { int twice(int x) { return 2 * x; } }
namespace b { using a::twice; }
using b::twice;

int main()
{
    int n = 7;
    char line[16];
    printf("scanf %d, n still %d\n", scanf("%d", &n), n);
    printf("fgets %s\n", fgets(line, sizeof line, stdin) ? "a line" : "null");
    printf("twice %d\n", twice(21));
    return 0;
}
