#include <cstdio>
#include <memory>
#include <utility>
#include <type_traits>

typedef std::pair<const int, std::unique_ptr<int> > V;
int main() {
    std::printf("%d %d %d %d\n", (int)std::is_constructible<V, std::pair<int, std::unique_ptr<int> > &&>::value,
                (int)std::is_convertible<std::unique_ptr<int>, std::unique_ptr<int> >::value,
                (int)std::is_convertible<int, const int>::value, (int)std::is_constructible<V, int, std::unique_ptr<int> >::value);
}
