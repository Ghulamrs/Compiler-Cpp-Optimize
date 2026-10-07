// [expr.add]/6: two pointers to different object types have no difference.
int main() {
    char c = 0;
    int i = 0;
    char *p = &c;
    int *q = &i;
    return (int)(p - q);
}
