#include "../lib/libc.h"

static const char help_text[] =
    "commands: shell, exit, help, clear, echo\r\n"
    "  ls, ps, mem, cat, cp, mv, del\r\n"
    "  cd, mkdir, rmdir\r\n";

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    (void)write(help_text, strlen(help_text));
    return 0;
}
