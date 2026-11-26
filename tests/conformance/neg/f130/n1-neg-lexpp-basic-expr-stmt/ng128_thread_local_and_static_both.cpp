int main() { static thread_local int x = 0; static static int y = 0; return x + y; }
