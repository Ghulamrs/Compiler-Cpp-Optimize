const int g = 1; int main() { int *p = reinterpret_cast<int*>(&g); return *p; }
