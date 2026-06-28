#include "../lib/partos.h"

static void ps_write_char(char c)
{
    (void)app_write_buffer(&c, 1u);
}

static void ps_write_name(const process_t *proc)
{
    uint8_t len = 0;

    while ((len != MAX_PNAME_LEN) && (proc->pname[len] != 0)) {
        len++;
    }
    if (len != 0u) {
        (void)app_write_buffer(proc->pname, len);
    }
}

static char ps_state_char(const process_t *proc)
{
    thread_t *thread = proc->main_thread;

    if (thread == 0) {
        return '?';
    }

    switch (thread->state) {
    case THREAD_STATE_SUSPENDED:
        return 'S';
    case THREAD_STATE_RUNNING:
        return 'R';
    case THREAD_STATE_WAITING:
        return 'W';
    case THREAD_STATE_JOINED:
        return 'J';
    case THREAD_STATE_TERMINATED:
        return 'T';
    default:
        return '?';
    }
}

static char ps_bank_char(const process_t *proc)
{
    thread_t *thread = proc->main_thread;

    if (thread == 0) {
        return '?';
    }
    return (char)('0' + thread->bank);
}

static void ps_write_process_line(process_t *proc, process_t *current_process)
{
    ps_write_char(proc == current_process ? '*' : ' ');
    ps_write_char(' ');
    ps_write_name(proc);
    ps_write_char(' ');
    ps_write_char('[');
    ps_write_char(ps_state_char(proc));
    ps_write_char(' ');
    ps_write_char('b');
    ps_write_char(ps_bank_char(proc));
    ps_write_char(']');
    app_write_newline();
}

int main(int argc, char **argv)
{
    sys_info_t *info;
    process_t *proc;
    process_t *current_process = 0;

    (void)argc;
    (void)argv;

    info = app_sys_info();
    if (info == 0) {
        return 1;
    }

    if (info->current_thread != 0) {
        current_process = (process_t *)info->current_thread->process;
    }

    proc = info->first_process;
    while (proc != 0) {
        ps_write_process_line(proc, current_process);
        proc = (process_t *)app_read_u16(proc, PROCESS_NEXT_OFF);
    }

    return 0;
}
