#include <cstdio>
#include <memory>
#include <utility>
#include <type_traits>
#include <map>
typedef std::pair<const int, std::unique_ptr<int> > V;
int main() {
    std::printf("%d %d %d %d\n", (int)std::is_constructible<V, std::pair<int, std::unique_ptr<int> > &&>::value,
                (int)std::is_convertible<std::unique_ptr<int>, std::unique_ptr<int> >::value,
                (int)std::is_convertible<int, const int>::value, (int)std::is_constructible<V, int, std::unique_ptr<int> >::value);
    std::map<int, std::unique_ptr<int> > owners;
    owners[1].reset(new int(10));
    std::printf("a\n");
    owners.emplace(2, std::unique_ptr<int>(new int(20)));
    std::printf("b\n");
    owners.insert(std::make_pair(3, std::unique_ptr<int>(new int(30))));
    std::printf("c %d\n", (int)owners.size());
}
