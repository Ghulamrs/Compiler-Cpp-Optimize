#include "Driver.h"
#include "Name.h"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#else
#include <climits>
#include <cstdlib>
#endif

// A file made by this run, by its full name: C:\\...\\hello.exe or /Users/.../hello.
static std::string fullName(const std::string &file) {
#ifdef _WIN32
    char full[_MAX_PATH];
    return _fullpath(full, file.c_str(), sizeof full) ? std::string(full) : file;
#else
    char full[PATH_MAX];
    return realpath(file.c_str(), full) ? std::string(full) : file;
#endif
}

int main(int argc, char **argv) {
    Driver driver;
    int status = driver.run(argc, argv);
    // Said in so many words, as other compilers' IDEs do: on stderr beside the banner, and like it
    // left out by -nologo. Only for a run that compiled and finished with nothing wrong.
    if (status == 0 && driver.saysDone()) {
        for (const std::string &file : driver.produced())
            if (!file.empty()) std::fprintf(stderr, "%s\n", fullName(file).c_str());
        // A file compiled below the -O asked for is said again here, not left to a note further up (review D12).
        const int downgraded = driver.volatileDowngrades();
        if (downgraded > 0)
            std::fprintf(stderr, "%s: compilation completed successfully - 0 errors, %d file%s compiled without -O "
                         "because %s 'volatile'\n", program::kName, downgraded, downgraded == 1 ? "" : "s",
                         downgraded == 1 ? "it uses" : "they use");
        else
            std::fprintf(stderr, "%s: compilation completed successfully - 0 errors\n", program::kName);
    }
    return status;
}
