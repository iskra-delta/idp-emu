#include "../lib/partos.h"

int main(int argc, char **argv)
{
    char *text;
    uint16_t len = 0;

    (void)argc;
    (void)argv;

    text = app_arg_start();
    while (text[len] != 0) {
        len++;
    }
    if (len != 0u) {
        (void)app_write_buffer(text, len);
    }
    app_write_newline();
    return 0;
}
