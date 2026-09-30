// [expr.cond]/6 and [expr]/13: an int * and a double * have no composite
// pointer type, so the two arms of '?:' have none to meet at.
int main() {
    int i = 1;
    double d = 2.0;
    int *pi = &i;
    double *pd = &d;
    return *(int *)(i ? pi : pd);
}
