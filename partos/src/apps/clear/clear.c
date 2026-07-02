#include "../lib/libc.h"

int main(int argc, char **argv)
{
    (void)argv;

    if (argc != 1) {
        puts("usage: clear");
        return 1;
    }

    (void)app_clear_screen();
    return 0;
}
