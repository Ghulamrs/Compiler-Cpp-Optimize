#include <system_error>
#include <iostream>
#include <sstream>
int main() {
    std::istringstream in("x");
    in.exceptions(std::ios::failbit);
    try { int v; in >> v; }
    catch (const std::ios_base::failure &e) {
        std::cout << e.what() << "|" << (e.code() == std::io_errc::stream) << "|" << e.code().category().name() << "\n";
    }
    return 0;
}
