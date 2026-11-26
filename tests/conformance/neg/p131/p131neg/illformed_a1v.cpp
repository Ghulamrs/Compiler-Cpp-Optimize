int main() { int k = 2; const int i = k; int *p = static_cast<int*>(&i); return *p; }
