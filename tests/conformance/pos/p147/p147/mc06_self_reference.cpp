// A member class template that names itself and its siblings: a list node, and a default argument from the class.
#include <cstdio>
template <class T> struct List {
    template <class U, class V = T> struct Node { U val; V extra; Node *next; };
    Node<int> first, second;
    int sum() const { int s = 0; for (const Node<int> *n = &first; n != 0; n = n->next) s += n->val + (int)n->extra; return s; }
};
int main() {
    List<char> l;
    l.first.val = 3; l.first.extra = 1; l.first.next = &l.second;
    l.second.val = 4; l.second.extra = 2; l.second.next = 0;
    std::printf("%d %d\n", l.sum(), (int)sizeof(List<char>::Node<long, char>));
    return 0;
}
