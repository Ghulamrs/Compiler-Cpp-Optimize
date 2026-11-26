#include <locale>

// [locale.id]: locale::id has a deleted copy constructor.
int main() {
    std::locale::id a;
    std::locale::id b(a);
    return 0;
}
