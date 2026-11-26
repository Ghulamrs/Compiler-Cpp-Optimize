// [temp.friend]/1: tpeek is befriended, tget is not.
class B { int secret = 2; template <class T> friend int tpeek(const B &); };
template <class T> int tpeek(const B &b) { return b.secret; }
template <class T> int tget(const B &b) { return b.secret; }
int main() { return tpeek<int>(B()) + tget<int>(B()); }