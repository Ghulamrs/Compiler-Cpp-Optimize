#include <cstdio>
#include <new>
#include <string>
#include <utility>
template <class P, class... A> P *make(void *raw, A &&... a) { return ::new (raw) P(std::forward<A>(a)...); }
template <class P, class... A> P mk(A &&... a) { return P(std::forward<A>(a)...); }
int main() {
    setvbuf(stdout, 0, _IONBF, 0);
    std::pair<const std::string, int> c = mk<std::pair<const std::string, int> >("cast", 1);
    std::printf("cast %s\n", c.first.c_str());
    void *raw = ::operator new(64);
    std::pair<std::string, int> *p = make<std::pair<std::string, int> >(raw, "primary", 2);
    std::printf("primary %s %d\n", p->first.c_str(), p->second);
    std::pair<const std::string, int> *q = make<std::pair<const std::string, int> >(raw, "partial", 3);
    std::printf("partial %s %d\n", q->first.c_str(), q->second);
}
