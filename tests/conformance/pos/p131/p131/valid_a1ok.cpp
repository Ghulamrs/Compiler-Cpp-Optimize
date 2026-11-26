int main() { const int i = 1; const int *p = static_cast<const int*>(&i); void const *q = static_cast<const void*>(p); return *p + (q != 0); }
