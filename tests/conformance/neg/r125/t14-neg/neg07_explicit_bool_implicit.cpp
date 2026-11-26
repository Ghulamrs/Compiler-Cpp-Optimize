struct Flag { explicit operator bool() const { return true; } };
int main() { Flag f; bool b = f; return b; }
