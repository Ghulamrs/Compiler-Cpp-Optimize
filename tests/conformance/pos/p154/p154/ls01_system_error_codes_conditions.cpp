#include <system_error>
#include <cstdio>
#include <cerrno>
enum class my_err { boom = 1 };
struct my_cat_t : std::error_category {
    const char *name() const noexcept { return "mine"; }
    std::string message(int) const { return "boom happened"; }
    std::error_condition default_error_condition(int) const noexcept { return std::errc::io_error; }
};
const std::error_category &my_cat() { static my_cat_t c; return c; }
namespace std { template <> struct is_error_code_enum<my_err> : true_type {}; }
std::error_code make_error_code(my_err e) { return std::error_code(static_cast<int>(e), my_cat()); }
int main() {
    std::error_code ec = std::make_error_code(std::errc::no_such_file_or_directory);
    std::printf("%d %s %d\n", ec.value() == ENOENT, ec.category().name(), (int)(bool)ec);
    std::printf("%d\n", (int)(ec == std::errc::no_such_file_or_directory));
    std::error_code e2 = my_err::boom;
    std::printf("%s %s %d %d\n", e2.category().name(), e2.message().c_str(), (int)(e2 == std::errc::io_error), (int)(e2 == ec));
    std::error_code none;
    std::printf("%d %s\n", (int)(bool)none, none.category().name());
    try {
        throw std::system_error(std::make_error_code(std::errc::invalid_argument), "opening");
    } catch (const std::runtime_error &e) {
        const std::system_error &se = dynamic_cast<const std::system_error &>(e);
        std::printf("%d %d\n", se.code().value() == EINVAL, (int)(std::string(e.what()).find("opening: ") == 0));
    }
    std::error_condition c = std::errc::timed_out;
    std::printf("%d %d\n", c.value() == ETIMEDOUT, (int)(&c.category() == &std::generic_category()));
    std::printf("%d\n", (int)(std::system_category().default_error_condition(2) == std::errc::no_such_file_or_directory));
    std::hash<std::error_code> h;
    std::printf("%d\n", (int)(h(ec) == h(std::make_error_code(std::errc::no_such_file_or_directory))));
    return 0;
}
