int main() { const int i = 1; int *p = static_cast<int*>(&i); return *p; }
