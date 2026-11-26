thread_local int counter = 3;
int main() { counter++; return counter == 4 ? 0 : 1; }
