#include <locale>

// [locale.numpunct]: a facet's destructor is protected - only a locale, or a derived class, may destroy it.
int main() {
    std::numpunct<char> *p = new std::numpunct<char>;
    delete p;
    return 0;
}
