// std::exception::what() through a base reference, for the library's hierarchy; and a user class derived from std::exception.
#include <cstdio>
#include <stdexcept>
#include <exception>
#include <string>
struct Mine : std::exception { const char *what() const noexcept override { return "mine"; } };
int main() {
    try { throw std::runtime_error("rt"); } catch (const std::exception &e) { std::printf("%s\n", e.what()); }
    try { throw std::logic_error("lg"); } catch (const std::logic_error &e) { std::printf("%s\n", e.what()); }
    try { throw std::out_of_range("oor"); } catch (const std::exception &e) { std::printf("%s\n", e.what()); }
    try { throw Mine(); } catch (const std::exception &e) { std::printf("%s\n", e.what()); }
    try { throw std::runtime_error(std::string("built")); } catch (const std::runtime_error &e) { std::printf("%s\n", e.what()); }
    return 0;
}
