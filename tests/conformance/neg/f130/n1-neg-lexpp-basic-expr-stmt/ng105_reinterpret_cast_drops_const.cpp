int main() { const int i = 1; int *p = reinterpret_cast<int*>(&i); return *p; }
