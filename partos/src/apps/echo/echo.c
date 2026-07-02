#include "../lib/libc.h"

int main(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        if (i != 1) {
            (void)putchar(' ');
        }
        (void)write(argv[i], strlen(argv[i]));
    }
    (void)puts("");
    return 0;
}
