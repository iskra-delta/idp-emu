#include "../lib/libc.h"

typedef struct ps_stats_s {
    uint16_t threads;
    uint16_t user_blocks;
    uint16_t user_bytes;
    uint16_t sys_blocks;
    uint16_t sys_bytes;
    uint16_t events;
    uint16_t timers;
    uint16_t services;
} ps_stats_t;

static sys_info_t *ps_info;

static void ps_write_text(const char *s)
{
    (void)write(s, strlen(s));
}

static void ps_write_char(char c)
{
    (void)putchar(c);
}

static void ps_write_name(const process_t *proc)
{
    uint8_t len = 0u;

    while ((len != MAX_PNAME_LEN) && (proc->pname[len] != 0)) {
        len++;
    }
    if (len != 0u) {
        (void)write(proc->pname, len);
    }
}

static void ps_write_dec16(uint16_t value)
{
    static const uint16_t powers[5] = { 10000u, 1000u, 100u, 10u, 1u };
    uint8_t started = 0u;
    uint8_t i;

    for (i = 0u; i != 5u; ++i) {
        uint8_t digit = 0u;

        while (value >= powers[i]) {
            value = (uint16_t)(value - powers[i]);
            digit++;
        }
        if ((digit != 0u) || (started != 0u) || (i == 4u)) {
            ps_write_char((char)('0' + digit));
            started = 1u;
        }
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

static thread_t *ps_next_thread(thread_t *thread)
{
    return (thread_t *)app_read_u16(thread, 0u);
}

static block_t *ps_next_block(block_t *block)
{
    return (block_t *)app_read_u16(block, BLOCK_NEXT_OFF);
}

static event_t *ps_next_event(event_t *event)
{
    return (event_t *)app_read_u16(event, 0u);
}

static timer_t *ps_next_timer(timer_t *timer)
{
    return (timer_t *)app_read_u16(timer, 0u);
}

static service_t *ps_next_service(service_t *service)
{
    return (service_t *)app_read_u16(service, 0u);
}

static process_t *ps_next_process(process_t *proc)
{
    return (process_t *)app_read_u16(proc, PROCESS_NEXT_OFF);
}

static uint8_t ps_thread_ptr_in_list(thread_t *thread,
                                     const process_t *proc,
                                     const void *ptr)
{
    while (thread != 0) {
        if (((const void *)thread == ptr) && (thread->process == proc)) {
            return 1u;
        }
        thread = ps_next_thread(thread);
    }
    return 0u;
}

static uint8_t ps_owner_matches(const process_t *proc, const void *owner)
{
    if ((proc == 0) || (owner == 0)) {
        return 0u;
    }
    if (owner == proc) {
        return 1u;
    }
    if (ps_info == 0) {
        return 0u;
    }
    if (ps_thread_ptr_in_list(ps_info->first_running_thread, proc, owner)) {
        return 1u;
    }
    if (ps_thread_ptr_in_list(ps_info->first_waiting_thread, proc, owner)) {
        return 1u;
    }
    if (ps_thread_ptr_in_list(ps_info->first_suspended_thread, proc, owner)) {
        return 1u;
    }
    return ps_thread_ptr_in_list(ps_info->first_terminated_thread, proc, owner);
}

static uint16_t ps_count_threads_in_list(thread_t *thread, const process_t *proc)
{
    uint16_t count = 0u;

    while (thread != 0) {
        if (thread->process == proc) {
            count = (uint16_t)(count + 1u);
        }
        thread = ps_next_thread(thread);
    }
    return count;
}

static void ps_scan_heap(block_t *block, const process_t *proc,
                         uint16_t *blocks, uint16_t *bytes)
{
    while (block != 0) {
        if (((block->stat & ALLOCATED) != 0u) &&
            ps_owner_matches(proc, block->hdr.owner)) {
            *blocks = (uint16_t)(*blocks + 1u);
            *bytes = (uint16_t)(*bytes + block->size);
        }
        block = ps_next_block(block);
    }
}

static uint16_t ps_count_events(const process_t *proc)
{
    uint16_t count = 0u;
    event_t *event = ps_info->first_event;

    while (event != 0) {
        if (ps_owner_matches(proc, event->hdr.owner)) {
            count = (uint16_t)(count + 1u);
        }
        event = ps_next_event(event);
    }
    return count;
}

static uint16_t ps_count_timers(const process_t *proc)
{
    uint16_t count = 0u;
    timer_t *timer = ps_info->first_timer;

    while (timer != 0) {
        if (ps_owner_matches(proc, timer->hdr.owner)) {
            count = (uint16_t)(count + 1u);
        }
        timer = ps_next_timer(timer);
    }
    return count;
}

static uint16_t ps_count_services(const process_t *proc)
{
    uint16_t count = 0u;
    service_t *service = ps_info->first_service;

    while (service != 0) {
        if (ps_owner_matches(proc, service->hdr.owner)) {
            count = (uint16_t)(count + 1u);
        }
        service = ps_next_service(service);
    }
    return count;
}

static void ps_collect_stats(const process_t *proc, ps_stats_t *stats)
{
    stats->threads = 0u;
    stats->user_blocks = 0u;
    stats->user_bytes = 0u;
    stats->sys_blocks = 0u;
    stats->sys_bytes = 0u;
    stats->events = 0u;
    stats->timers = 0u;
    stats->services = 0u;

    if ((proc == 0) || (ps_info == 0)) {
        return;
    }

    stats->threads = (uint16_t)(
        ps_count_threads_in_list(ps_info->first_running_thread, proc) +
        ps_count_threads_in_list(ps_info->first_waiting_thread, proc) +
        ps_count_threads_in_list(ps_info->first_suspended_thread, proc) +
        ps_count_threads_in_list(ps_info->first_terminated_thread, proc));

    ps_scan_heap((block_t *)ps_info->user_heap, proc,
                 &stats->user_blocks, &stats->user_bytes);
    ps_scan_heap((block_t *)ps_info->system_heap, proc,
                 &stats->sys_blocks, &stats->sys_bytes);

    stats->events = ps_count_events(proc);
    stats->timers = ps_count_timers(proc);
    stats->services = ps_count_services(proc);
}

static void ps_write_process_line(process_t *proc, process_t *current_process)
{
    ps_stats_t stats;

    ps_collect_stats(proc, &stats);

    ps_write_char(proc == current_process ? '*' : ' ');
    ps_write_char(' ');
    ps_write_name(proc);
    ps_write_char(' ');
    ps_write_char('[');
    ps_write_char(ps_state_char(proc));
    ps_write_char(' ');
    ps_write_char('b');
    ps_write_char(ps_bank_char(proc));
    if ((proc->pflags & PROCESS_INTERNAL) != 0u) {
        ps_write_text(" i");
    }
    ps_write_char(']');
    ps_write_text(" thr=");
    ps_write_dec16(stats.threads);
    ps_write_text(" usr=");
    ps_write_dec16(stats.user_blocks);
    ps_write_char('/');
    app_write_hex16(stats.user_bytes);
    ps_write_text(" sys=");
    ps_write_dec16(stats.sys_blocks);
    ps_write_char('/');
    app_write_hex16(stats.sys_bytes);
    app_write_newline();

    ps_write_text("  evt=");
    ps_write_dec16(stats.events);
    ps_write_text(" tmr=");
    ps_write_dec16(stats.timers);
    ps_write_text(" svc=");
    ps_write_dec16(stats.services);
    app_write_newline();
}

int main(int argc, char **argv)
{
    process_t *proc;
    process_t *current_process = 0;

    if (argc != 1) {
        puts("usage: ps");
        return 1;
    }
    (void)argv;

    ps_info = app_sys_info();
    if (ps_info == 0) {
        return 1;
    }

    if (ps_info->current_thread != 0) {
        current_process = (process_t *)ps_info->current_thread->process;
    }

    proc = ps_info->first_process;
    while (proc != 0) {
        ps_write_process_line(proc, current_process);
        proc = ps_next_process(proc);
    }

    return 0;
}
