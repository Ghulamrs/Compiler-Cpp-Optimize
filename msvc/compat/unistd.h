/* <unistd.h> for MSVC, which does not have one.
 *
 * This exists so that the compiler's own source needs no change to build here.
 * src/Driver.cpp includes <unistd.h> and uses two things out of it - getpid,
 * to keep two cc1 runs in one directory from choosing the same temporary
 * name, and getcwd, for the directory -g records so a debugger can resolve a
 * relative file name. Windows has both under other names in other headers.
 *
 * So this directory goes on the include path ahead of everything else, this
 * file answers the include, and nothing in src/ knows the difference. Three
 * lines of shim against a platform #ifdef in a file that is otherwise about
 * compiling C: the shim is the smaller change, and it keeps the port in the
 * project that wants it rather than in the compiler that does not.
 *
 * getcwd was the second, and it arrived the way this comment expected: the
 * Mac and the Linux box both compiled it without comment and MSVC would not,
 * which is the whole argument for building on all three. An include that
 * resolves here rather than to the real header is a decision, and it should
 * stay a visible one.
 */
#ifndef CC1_MSVC_COMPAT_UNISTD_H
#define CC1_MSVC_COMPAT_UNISTD_H

#include <direct.h>
#include <process.h>

/* Both are the same functions under Microsoft's leading-underscore rule for
 * names POSIX defines but C does not reserve. _getcwd lives in <direct.h>
 * rather than <process.h>, which is the only reason there are two includes. */
#define getpid _getpid
#define getcwd _getcwd

#endif
