#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
struct Odd { bool operator()(int x) const { return x % 2 != 0; } };
struct Counter { int n; Counter() : n(0) {} int operator()() { return n += 3; } };
struct Lcg {
    typedef unsigned int result_type;
    unsigned int s;
    static constexpr unsigned int min() { return 0; }
    static constexpr unsigned int max() { return 0x7fff; }
    unsigned int operator()() { s = s * 1103515245u + 12345u; return (s >> 16) & 0x7fff; }
};
struct Below { int operator()(int n) const { return n / 2; } };
void show(const char *what, const int *from, const int *to) {
    std::printf("%s:", what);
    for (; from != to; ++from) std::printf(" %d", *from);
    std::printf("\n");
}
int main() {
    int a[] = {1, 2, 3, 4, 5, 6, 7, 8};
    int out[12] = {0};
    int *end = std::copy_n(a, 3, out);
    show("copy_n", out, end);
    end = std::copy_if(a, a + 8, out, Odd());
    show("copy_if", out, end);
    int b[8] = {0};
    int *start = std::copy_backward(a, a + 5, b + 8);
    std::printf("%d ", (int)(start - b));
    show("copy_backward", b, b + 8);
    std::vector<std::string> from;
    from.push_back("alpha");
    from.push_back("beta");
    std::vector<std::string> to(3);
    std::move(from.begin(), from.end(), to.begin() + 1);
    std::printf("move [%s][%s][%s] %d %d\n", to[0].c_str(), to[1].c_str(), to[2].c_str(), (int)from[0].size(), (int)from[1].size());
    std::move_backward(to.begin(), to.begin() + 2, to.end());
    std::printf("move_backward [%s][%s]\n", to[1].c_str(), to[2].c_str());
    int c[] = {10, 20, 30};
    int *after = std::swap_ranges(a, a + 3, c);
    std::printf("%d ", (int)(after - c));
    show("swap_ranges", a, a + 3);
    std::iter_swap(a, a + 7);
    show("iter_swap", a, a + 8);
    std::transform(a, a + 8, out, std::negate<int>());
    show("transform", out, out + 8);
    std::transform(a, a + 3, c, out, std::plus<int>());
    show("transform2", out, out + 3);
    int r[] = {1, 2, 1, 3, 1};
    std::replace(r, r + 5, 1, 9);
    show("replace", r, r + 5);
    std::replace_if(r, r + 5, Odd(), 0);
    show("replace_if", r, r + 5);
    int r2[] = {4, 5, 4, 6};
    end = std::replace_copy(r2, r2 + 4, out, 4, 7);
    show("replace_copy", out, end);
    end = std::replace_copy_if(r2, r2 + 4, out, Odd(), -1);
    show("replace_copy_if", out, end);
    std::fill(out, out + 4, 5);
    end = std::fill_n(out + 4, 2, 6);
    std::printf("%d ", (int)(end - out));
    show("fill", out, out + 6);
    std::generate(out, out + 3, Counter());
    end = std::generate_n(out + 3, 2, Counter());
    std::printf("%d ", (int)(end - out));
    show("generate", out, out + 5);
    int d[] = {1, 0, 2, 0, 3, 0, 0, 4};
    end = std::remove(d, d + 8, 0);
    show("remove", d, end);
    int d2[] = {1, 2, 3, 4, 5, 6, 7};
    end = std::remove_if(d2, d2 + 7, Odd());
    show("remove_if", d2, end);
    int d3[] = {1, 0, 2, 0, 3};
    end = std::remove_copy(d3, d3 + 5, out, 0);
    show("remove_copy", out, end);
    end = std::remove_copy_if(d3, d3 + 5, out, Odd());
    show("remove_copy_if", out, end);
    int u[] = {1, 1, 2, 2, 2, 3, 1, 1, 4};
    end = std::unique(u, u + 9);
    show("unique", u, end);
    int u2[] = {1, 3, 5, 2, 4, 7, 9, 8};
    end = std::unique(u2, u2 + 8, std::less<int>());
    show("unique_pred", u2, end);
    int u3[] = {5, 5, 6, 6, 6, 5};
    end = std::unique_copy(u3, u3 + 6, out);
    show("unique_copy", out, end);
    end = std::reverse_copy(a, a + 5, out);
    show("reverse_copy", out, end);
    int t[] = {1, 2, 3, 4, 5, 6, 7};
    int *mid = std::rotate(t, t + 3, t + 7);
    std::printf("%d ", (int)(mid - t));
    show("rotate", t, t + 7);
    mid = std::rotate(t, t, t + 7);
    std::printf("%d %d\n", (int)(mid - t), (int)(std::rotate(t, t + 7, t + 7) - t));
    end = std::rotate_copy(t, t + 2, t + 7, out);
    show("rotate_copy", out, end);
    std::vector<int> v;
    for (int i = 0; i < 40; i++) v.push_back(i);
    std::vector<int> w = v;
    std::random_shuffle(w.begin(), w.end());
    std::sort(w.begin(), w.end());
    std::printf("random_shuffle %d\n", (int)(w == v));
    std::random_shuffle(w.begin(), w.end(), Below());
    std::sort(w.begin(), w.end());
    std::printf("random_shuffle(gen) %d\n", (int)(w == v));
    Lcg g;
    g.s = 7;
    std::shuffle(w.begin(), w.end(), g);
    std::printf("shuffle %d", (int)(w != v));
    std::sort(w.begin(), w.end());
    std::printf(" %d\n", (int)(w == v));
    return 0;
}
