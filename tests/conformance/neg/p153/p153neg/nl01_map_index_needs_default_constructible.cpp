#include <map>
struct NoDefault {
    explicit NoDefault(int v) : v(v) {}
    int v;
};
int main() {
    std::map<int, NoDefault> m;
    return m[1].v;
}
