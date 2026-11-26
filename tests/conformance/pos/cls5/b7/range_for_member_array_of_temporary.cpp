#include <cstdio>
struct H {
    int a[2];
    int id;
    H(int i) : id(i) { a[0] = i; a[1] = i + 1; std::printf("+%d ", id); }
    ~H() { std::printf("-%d ", id); }
};
H make(int i) { return H(i); }
int main() {
    for (int x : H(1).a) {
        if (x == 2) break;
        std::printf("%d ", x);
    }
    std::printf("| ");
    try {
        for (int x : make(3).a) {
            if (x == 4) throw x;
            std::printf("%d ", x);
        }
    } catch (int e) {
        std::printf("c%d ", e);
    }
    std::printf("| ");
    for (int x : H(5).a) std::printf("%d ", x);
    std::printf("\n");
    return 0;
}
