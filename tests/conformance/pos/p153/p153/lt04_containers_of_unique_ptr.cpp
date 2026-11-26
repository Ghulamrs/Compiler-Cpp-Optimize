#include <cstdio>
#include <memory>
#include <vector>
#include <list>
#include <map>
int main() {
    std::vector<std::unique_ptr<int> > v;
    v.push_back(std::unique_ptr<int>(new int(1)));
    std::list<std::unique_ptr<int> > l;
    l.push_back(std::unique_ptr<int>(new int(2)));
    std::printf("%d %d\n", *v[0], *l.front());
    std::map<int, std::unique_ptr<int> > m;
    m[1].reset(new int(3));
    std::printf("%d\n", *m[1]);
}
