#include <cstdio>
void f() { return void(); }
int main() {
    void();
    (void)0, void();
    decltype(void()) *p = 0;
    std::printf("%d\n", p == 0);
}
