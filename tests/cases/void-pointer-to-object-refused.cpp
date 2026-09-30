// [conv.ptr]/2: an object pointer converts to void * implicitly, and void * converts
// back only with a cast. C allows the implicit way back and C++ does not; cpp11
// accepted it as C's rule until 2026-09-30 (the review's A7).
extern "C" { void *malloc(unsigned long); }
struct P { int x; };
int main() {
    void *raw = malloc(sizeof(P));
    P *p = static_cast<P *>(raw);   // a cast: fine
    void *back = p;                 // to void *: fine
    P *q = back;                    // back without a cast: refused
    return q->x;
}
