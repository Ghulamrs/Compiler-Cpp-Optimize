// [temp.arg.explicit]/3: some template arguments written, the rest deduced - conv<int>, get<I>, make_shared, the pointer casts.
#include <cstdio>
#include <memory>
#include <utility>
template <class T, class S> T conv(S s) { return (T)s; }
struct B { virtual ~B() {} int b = 1; };
struct D : B { int d = 2; };
int main() {
    std::pair<int, char> p(3, 'x');
    std::shared_ptr<D> sd = std::make_shared<D>();
    std::shared_ptr<B> sb = sd;
    std::shared_ptr<D> back = std::static_pointer_cast<D>(sb);
    std::shared_ptr<D> dyn = std::dynamic_pointer_cast<D>(sb);
    std::shared_ptr<const D> cd = std::const_pointer_cast<const D>(sd);
    std::printf("%d %d %c %d %d %d %d\n", conv<int>(2.5), std::get<0>(p), std::get<1>(p), back->d, dyn->d, cd->d,
                (int)sd.use_count());
    return 0;
}
