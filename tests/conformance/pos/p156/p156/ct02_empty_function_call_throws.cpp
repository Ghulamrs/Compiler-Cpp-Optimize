#include <cstdio>
#include <functional>
int main() {
    std::function<int(int)> f;
    std::printf("%d\n", (int)(bool)f);
    try {
        f(1);
    } catch (const std::bad_function_call &) {
        std::printf("bad call\n");
    }
    return 0;
}
