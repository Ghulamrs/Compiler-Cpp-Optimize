#include <cstdio>
#include <iterator>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <algorithm>
int main() {
    int a[5] = {1, 2, 3, 4, 5};
    int *p = a;
    std::advance(p, 3);
    std::printf("%d %d %d %d\n", *p, (int)std::distance(a, a + 5), *std::next(a), *std::prev(a + 5, 2));
    std::vector<int> v;
    std::copy(a, a + 5, std::back_inserter(v));
    std::vector<int> w(2, 0);
    std::copy(a, a + 3, std::inserter(w, w.begin() + 1));
    std::printf("%d %d %d %d\n", (int)v.size(), v[4], (int)w.size(), w[3]);
    std::vector<std::string> s;
    s.push_back("alpha");
    s.push_back("beta");
    std::vector<std::string> t(std::make_move_iterator(s.begin()), std::make_move_iterator(s.end()));
    std::printf("%s %s %d %d\n", t[0].c_str(), t[1].c_str(), (int)s[0].size(), (int)(std::make_move_iterator(s.end()) - std::make_move_iterator(s.begin())));
    std::istringstream in("10 20 30");
    std::istream_iterator<int> first(in), last;
    int sum = 0;
    for (; first != last; ++first) sum += *first;
    std::printf("%d\n", sum);
    std::ostream_iterator<int> out(std::cout, ",");
    std::copy(a, a + 3, out);
    std::cout << std::endl;
    std::istringstream text("ab c");
    std::string all((std::istreambuf_iterator<char>(text)), std::istreambuf_iterator<char>());
    std::printf("[%s]\n", all.c_str());
    std::ostreambuf_iterator<char> o(std::cout);
    *o = 'x';
    *o = '\n';
    std::printf("%d %d %d\n", *std::begin(a), *(std::end(a) - 1), (int)(std::end(v) - std::begin(v)));
    std::reverse_iterator<int *> r(a + 5);
    std::printf("%d %d\n", *r, r[2]);
    return 0;
}
