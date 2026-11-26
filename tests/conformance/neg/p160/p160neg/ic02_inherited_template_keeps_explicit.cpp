// [class.inhctor]/4: the inherited constructor keeps its `explicit`, a template's specialization too.
template <class X> struct Q { X x; template <class U> explicit Q(U u) : x(u) {} };
template <class X> struct R : Q<X> { using Q<X>::Q; };
int main() {
    R<long> r = 3;
    return (int)r.x;
}
