// Two handlers of the same type: ill-formed? No - only a warning. Instead: catch by rvalue reference is ill-formed.
int main() { try { throw 1; } catch (int &&) {} return 0; }
