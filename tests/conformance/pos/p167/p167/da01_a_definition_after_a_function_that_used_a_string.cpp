// [dcl.fct.default]/4: a definition that repeats no default keeps the one its declaration gave, whatever was compiled
// between the two. 1.63 and 1.64 refused the definitions below - "every parameter after one with a default needs a
// default of its own" - because the function defined before them had used std::string: the last parameter list the
// compiler had read while instantiating it was merged into this function's defaults. 1.30 took them, and 1.65 does.
// (cppparse's PathUtil.cpp.)
#include <cstdio>
#include <string>
#include <vector>

namespace tool {
void split(const std::string &s, char sep, std::vector<std::string> &out, bool keepEmpty = false);
bool readList(const std::string &text, std::vector<std::string> &args);
void findFiles(const std::string &dir, std::vector<std::string> &headers, std::vector<std::string> &sources,
               std::vector<std::string> &dirs, int depth = 0);
int deeper(int depth = 0);
}

namespace tool {

bool readList(const std::string &text, std::vector<std::string> &args) {
    std::string tok;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if (c == ' ') {
            if (!tok.empty()) args.push_back(tok);
            tok.clear();
            continue;
        }
        tok += c;
    }
    return !args.empty();
}

// five parameters, the default on the last: the definition names none
void findFiles(const std::string &dir, std::vector<std::string> &headers, std::vector<std::string> &sources,
               std::vector<std::string> &dirs, int depth) {
    if (depth > 2) return;
    dirs.push_back(dir);
    (depth % 2 ? headers : sources).push_back(dir + "/f");
    findFiles(dir + "/d", headers, sources, dirs, depth + 1);
}

void local() { std::string t("x"); t += 'y'; }

// one parameter, and its default
int deeper(int depth) { return depth + 1; }

// a default in the middle of the declaration's list is not one: the last two have them
void split(const std::string &s, char sep, std::vector<std::string> &out, bool keepEmpty) {
    std::string part;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            if (keepEmpty || !part.empty()) out.push_back(part);
            part.clear();
        } else part += s[i];
    }
}

}   // namespace tool

int main() {
    std::vector<std::string> args, headers, sources, dirs, parts, all;
    std::printf("%d %d\n", tool::readList("-I a  b", args) ? 1 : 0, static_cast<int>(args.size()));
    tool::findFiles("top", headers, sources, dirs);                 // depth by its default
    std::printf("%d %d %d\n", static_cast<int>(headers.size()), static_cast<int>(sources.size()), static_cast<int>(dirs.size()));
    std::printf("%s\n", dirs.back().c_str());
    tool::findFiles("deep", headers, sources, dirs, 2);
    std::printf("%d\n", static_cast<int>(dirs.size()));
    tool::local();
    std::printf("%d %d\n", tool::deeper(), tool::deeper(4));
    tool::split("a,,b", ',', parts);                                // keepEmpty by its default
    tool::split("a,,b", ',', all, true);
    std::printf("%d %d\n", static_cast<int>(parts.size()), static_cast<int>(all.size()));
    return 0;
}
