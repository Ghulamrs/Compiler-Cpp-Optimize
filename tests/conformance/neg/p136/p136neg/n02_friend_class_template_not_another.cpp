// [temp.friend]/1: H's specializations are befriended, K's are not.
template <class T> class H { int secret = 1; template <class U> friend class H; };
template <class T> struct K { template <class U> int peek(const H<U> &o) const { return o.secret; } };
int main() { H<int> h; K<int> k; return k.peek(h); }