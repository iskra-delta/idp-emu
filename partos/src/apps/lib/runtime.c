#include "partos.h"

/* Service-table primitives provided by the app-side asm bridges. */
extern partos_t *app_bind_partos_service(void);   /* bootstrap.s */
extern void app_exit_process(void);               /* svccall.s   */
extern void app_dead(void);                        /* bootstrap.s */

static partos_t *app_partos_ptr;
static libc_t *app_libc_ptr;
static event_t *app_event_ptr;
static char app_cmdline_raw[APP_CMDLINE_CAP];
static char app_cmdline_parse[APP_CMDLINE_CAP];
static char *app_argv_data[APP_ARGV_MAX + 1u];
static int app_argc_value;
static const char app_empty_env[2] = { 0, 0 };
static const char *app_env_ptr = app_empty_env;

extern int main(int argc, char **argv);

static uint8_t app_copy_cstr_bounded_rt(char *dst, uint8_t cap, const char *src)
{
    uint8_t len = 0;

    if ((dst == 0) || (src == 0) || (cap == 0u)) {
        return 0u;
    }
    while (*src != 0) {
        if ((uint8_t)(len + 1u) >= cap) {
            dst[0] = 0;
            return 0u;
        }
        dst[len++] = *src++;
    }
    dst[len] = 0;
    return 1u;
}

static const char *app_find_env_value(const char *name)
{
    const char *env = app_env_ptr;
    const char *entry;
    const char *needle;

    if ((name == 0) || (*name == 0) || (env == 0)) {
        return 0;
    }
    while (*env != 0) {
        entry = env;
        needle = name;
        while ((*needle != 0) && (*entry == *needle)) {
            entry++;
            needle++;
        }
        if ((*needle == 0) && (*entry == '=')) {
            return entry + 1;
        }
        while (*env != 0) {
            env++;
        }
        env++;
    }
    return 0;
}

static void app_parse_argv(void)
{
    char *cursor = app_cmdline_parse;

    app_argc_value = 0;
    while (app_argc_value < (int)APP_ARGV_MAX) {
        cursor = app_skip_spaces(cursor);
        if (*cursor == 0) {
            break;
        }
        app_argv_data[app_argc_value++] = cursor;
        while ((*cursor != 0) && (*cursor != ' ')) {
            cursor++;
        }
        if (*cursor == 0) {
            break;
        }
        *cursor++ = 0;
    }
    app_argv_data[app_argc_value] = 0;
}

partos_t *app_partos(void)
{
    return app_partos_ptr;
}

libc_t *app_libc(void)
{
    return app_libc_ptr;
}

shell_t *app_shell(void)
{
    return (shell_t *)app_libc_ptr;
}

const char *app_command_line(void)
{
    return app_cmdline_raw;
}

const char *app_environment(void)
{
    return app_env_ptr;
}

const char *app_getenv(const char *name)
{
    return app_find_env_value(name);
}

int app_argc(void)
{
    return app_argc_value;
}

char **app_argv(void)
{
    return app_argv_data;
}

process_t *app_current_process(void)
{
    partos_t *partos = app_partos_ptr;
    sys_info_t *info;

    if ((partos == 0) || (partos->get_sys_info == 0)) {
        return 0;
    }
    info = partos->get_sys_info();
    if ((info == 0) || (info->current_thread == 0)) {
        return 0;
    }
    return (process_t *)info->current_thread->process;
}

event_t *app_open_event(void)
{
    partos_t *partos = app_partos_ptr;

    if (app_event_ptr != 0) {
        return app_event_ptr;
    }
    if ((partos == 0) || (partos->create_event == 0)) {
        return 0;
    }
    app_event_ptr = partos->create_event((void *)app_current_process());
    return app_event_ptr;
}

event_t *app_event(void)
{
    return app_event_ptr;
}

void app_close_event(void)
{
    partos_t *partos = app_partos_ptr;

    if (app_event_ptr == 0) {
        return;
    }
    if ((partos != 0) && (partos->destroy_event != 0)) {
        partos->destroy_event(app_event_ptr);
    }
    app_event_ptr = 0;
}

void app_wait_one(void)
{
    partos_t *partos = app_partos_ptr;
    event_t *evt = app_event_ptr;

    if ((evt == 0) || (partos == 0) || (partos->wait_for_events == 0)) {
        return;
    }
    if (partos->enable_interrupts != 0) {
        partos->enable_interrupts();
    }
    partos->wait_for_events(&evt, 1u);
}

void app_exit(void)
{
    app_close_event();
    if (app_partos_ptr != 0) {
        app_exit_process();
    }
    app_dead();
}

void app_bootstrap(void)
{
    process_t *process;
    const char *line;
    const char *env;
    int rc;

    app_partos_ptr = app_bind_partos_service();
    if (app_partos_ptr == 0) {
        app_dead();
    }
    if (app_partos_ptr->query_service != 0) {
        app_libc_ptr = (libc_t *)app_partos_ptr->query_service(LIBC_SERVICE_NAME);
    }

    process = app_current_process();
    line = 0;
    env = 0;
    if (process != 0) {
        line = process->cmdline;
        env = process->environment;
    }
    if ((line == 0) && (app_libc_ptr != 0) &&
        (app_libc_ptr->get_command_line != 0)) {
        line = app_libc_ptr->get_command_line();
    }
    if ((line == 0) && (app_partos_ptr->get_command_line != 0)) {
        line = app_partos_ptr->get_command_line();
    }
    if (line == 0) {
        line = "";
    }
    if ((env == 0) && (app_libc_ptr != 0) &&
        (app_libc_ptr->get_environment != 0)) {
        env = app_libc_ptr->get_environment();
    }
    if (env == 0) {
        env = app_empty_env;
    }
    app_env_ptr = env;
    if (!app_copy_cstr_bounded_rt(app_cmdline_raw, APP_CMDLINE_CAP, line)) {
        app_cmdline_raw[0] = 0;
    }
    if (!app_copy_cstr_bounded_rt(app_cmdline_parse, APP_CMDLINE_CAP,
                                  app_cmdline_raw)) {
        app_cmdline_parse[0] = 0;
    }
    app_parse_argv();

    rc = main(app_argc_value, app_argv_data);
    (void)rc;
    app_exit();
}
