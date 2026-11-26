// [string.cons]/15, [string.append], [string.assign], [string.insert], [string.replace]: the iterator-pair
// members, and an integral pair read as (count, unit).
#include <cstdio>
#include <string>
#include <vector>
int main() {
    const char *w = "range";
    std::vector<char> v(w, w + 5);
    std::string a(v.begin(), v.end());
    std::string b(3, 65);
    std::string c(w + 1, w + 4);
    std::string d("xy");
    d.append(w, w + 2);
    d.append(2, 'z');
    std::string e;
    e.assign(v.begin() + 1, v.end() - 1);
    std::string f("[]");
    f.insert(f.begin() + 1, w, w + 3);
    std::string g("abcdef");
    g.replace(g.begin() + 1, g.begin() + 3, v.begin(), v.begin() + 2);
    std::printf("%s %s %s %s %s %s %s %d\n", a.c_str(), b.c_str(), c.c_str(), d.c_str(), e.c_str(), f.c_str(),
                g.c_str(), (int)g.size());
    return 0;
}
