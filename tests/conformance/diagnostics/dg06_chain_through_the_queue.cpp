// a chain of function templates and a class, through the queue
template <class T> T leaf(T v) { return v.nothing(); }
template <class T> T mid(T v) { return leaf(v); }
template <class T> struct Box { T get(T v) { return mid(v); } };
int main() {
    Box<int> b;
    return b.get(1);
}
