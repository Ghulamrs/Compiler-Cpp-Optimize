int main() { double d = 1; int *p = static_cast<int *>(&d); return *p; }
