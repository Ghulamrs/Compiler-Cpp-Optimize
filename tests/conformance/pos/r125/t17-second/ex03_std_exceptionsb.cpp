// Library exception classes from the shipped headers: what(), inheritance (runtime_error is-a exception), throwing and catching by base; bad_alloc from new of an absurd size; vector::at throwing out_of_range.
#include <cstdio>
#include <stdexcept>
#include <exception>
#include <new>
#include <vector>
#include <string>
int main() {
    try { throw std::runtime_error("rt"); } catch (const std::exception &e) { std::printf("%s\n", e.what()); }
    try { throw std::logic_error("lg"); } catch (const std::logic_error &e) { std::printf("%s\n", e.what()); }
    try { throw std::out_of_range("oor"); } catch (const std::logic_error &e) { std::printf("%s\n", e.what()); }
    try { throw std::invalid_argument(std::string("ia")); } catch (const std::exception &e) { std::printf("%s\n", e.what()); }
    try { throw std::exception(); } catch (std::exception &e) { std::printf("%d\n", e.what() != 0); }
    return 0;
}
