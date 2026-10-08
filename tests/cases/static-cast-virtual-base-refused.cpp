// [expr.static.cast]/11: a static_cast from a base to a derived class is
// ill-formed where the base is a virtual base - the offset is not a constant.
struct V { int v; };
struct D : virtual V { int d; };

int main() {
    D d;
    V *pv = &d;
    D *pd = static_cast<D *>(pv);
    return pd->d;
}
