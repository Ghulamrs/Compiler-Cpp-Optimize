// The runtime hands a handler the pointer itself for a pointer type, so a
// reference to one would bind to a value, not to the exception object.
int g;
int main() {
    try { throw &g; } catch (int *&p) { return *p; }
    return 0;
}
