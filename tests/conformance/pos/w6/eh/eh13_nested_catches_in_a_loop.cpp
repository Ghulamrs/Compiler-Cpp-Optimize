// [except.handle]: the frame's handling state is right on every turn after a nested catch completed
#include <cstdio>
int main() {
    int sum = 0;
    for (int i = 0; i < 200; i++) {
        try { if (i % 3) throw i; sum += 1000; }
        catch (int e) {
            try { if (e % 2) throw e * 2; sum += e; }
            catch (int k) { sum += k; }
        }
    }
    std::printf("%d\n", sum);
    return 0;
}
