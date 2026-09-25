#define _XOPEN_SOURCE 700
#ifdef __APPLE__
#define _DARWIN_C_SOURCE /* expose mkdtemp in the macOS headers */
#endif
#include "test_util.h"
#include "scalar_output.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void)
{
    char directory[] = "tests/tmp_output_paths.XXXXXX";
    const int cwd = open(".", O_RDONLY);
    if (cwd < 0 || mkdtemp(directory) == NULL) {
        if (cwd >= 0) close(cwd);
        return 1;
    }
    if (chdir(directory) != 0) {
        close(cwd);
        rmdir(directory);
        return 1;
    }

    CHECK(symlink("missing.dat", "direct") == 0);
    CHECK(symlink("different.dat", "distinct") == 0);
    CHECK(mkdir("links", 0700) == 0);
    CHECK(symlink("../missing.dat", "links/next") == 0);
    CHECK(symlink("links/next", "chain") == 0);
    CHECK(output_paths_equal("direct", "missing.dat"));
    CHECK(output_paths_equal("missing.dat", "chain"));
    CHECK(output_paths_equal("direct", "chain"));
    CHECK(!output_paths_equal("direct", "distinct"));
    CHECK(access("missing.dat", F_OK) != 0); /* comparison must not create files */

    /* A readlink buffer must grow rather than silently truncate a long target. */
    char target[320];
    for (int i = 0; i < 150; i++) {
        memcpy(target + 2 * i, "./", 2);
    }
    strcpy(target + 300, "missing.dat");
    CHECK(symlink(target, "long") == 0);
    CHECK(output_paths_equal("long", "direct"));

    /* Cyclic links have no usable destination and must terminate resolution. */
    CHECK(symlink("loop_b", "loop_a") == 0);
    CHECK(symlink("loop_a", "loop_b") == 0);
    CHECK(!output_paths_equal("loop_a", "missing.dat"));

    const char *paths[] = {"direct", "distinct", "links/next", "chain",
                           "long", "loop_a", "loop_b"};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
        CHECK(unlink(paths[i]) == 0);
    }
    CHECK(rmdir("links") == 0);
    CHECK(fchdir(cwd) == 0);
    CHECK(close(cwd) == 0);
    CHECK(rmdir(directory) == 0);
    TEST_END();
}
