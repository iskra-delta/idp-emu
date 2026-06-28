#include "../lib/partos.h"

static const char help_text[] =
    "commands: shell, exit, help, clear, echo\r\n"
    "  ls, ps, mem, cat, cp, mv, del\r\n"
    "  cd, mkdir, rmdir, touch\r\n";

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    app_write_cstr(help_text);
    return 0;
}
