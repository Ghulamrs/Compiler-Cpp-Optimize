// The list `auto` deduces is std::initializer_list, which only its header declares.
int main() {
    auto x = {1, 2};
    return 0;
}
