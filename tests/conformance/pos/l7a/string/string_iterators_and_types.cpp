#include <cstdio>
#include <cstddef>
#include <string>
#include <type_traits>
int main() {
    std::string s = "abcdef";
    std::string back;
    for (std::string::reverse_iterator it = s.rbegin(); it != s.rend(); ++it) back += *it;
    std::string cback;
    for (std::string::const_reverse_iterator it = s.crbegin(); it != s.crend(); ++it) cback += *it;
    const std::string &cs = s;
    std::string::const_reverse_iterator mixed = s.rbegin();
    std::printf("%s %s %d %d\n", back.c_str(), cback.c_str(), (int)(mixed == cs.rbegin()), (int)(cs.rend() - s.rbegin()));
    std::string::reverse_iterator r = s.rbegin();
    std::printf("%c %c %c %c %d\n", r[1], *(r + 2), *(2 + r), *(r.base() - 1), (int)(s.rend() - (r + 4)));
    r += 3;
    --r;
    std::printf("%c %d %d\n", *r, (int)(r > s.rbegin()), (int)(r <= s.rbegin()));
    *s.rbegin() = 'F';
    std::string::iterator first = s.begin();
    std::string::const_iterator cfirst = s.cbegin();
    std::printf("%s %c %c %d\n", s.c_str(), *first, *(s.cend() - 1), (int)(cfirst == first));
    std::printf("%d %d %d %d %d %d\n",
        (int)std::is_same<std::string::value_type, char>::value,
        (int)std::is_same<std::string::size_type, std::size_t>::value,
        (int)std::is_same<std::string::difference_type, std::ptrdiff_t>::value,
        (int)std::is_same<std::string::traits_type, std::char_traits<char> >::value,
        (int)std::is_same<std::string::allocator_type, std::allocator<char> >::value,
        (int)std::is_same<std::string::reference, char &>::value);
    std::allocator<char> a = s.get_allocator();
    std::allocator<int> ai;
    int *block = ai.allocate(3);
    ai.construct(block, 7);
    ai.construct(block + 1, 8);
    std::printf("%d %d %d\n", (int)(a == std::allocator<char>()), block[0] + block[1], (int)(ai.max_size() > 1000));
    ai.destroy(block);
    ai.deallocate(block, 3);
    std::string big(100, 'x');
    big.resize(4);
    big.shrink_to_fit();
    std::string small = "tiny";
    small.shrink_to_fit();
    std::printf("%s %d %s %d\n", big.c_str(), (int)(big.capacity() >= big.size()), small.c_str(), (int)small.size());
    return 0;
}
