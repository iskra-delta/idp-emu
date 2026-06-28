#include "../lib/partos.h"

static uint16_t mem_used;
static uint16_t mem_free;
static uint16_t mem_self;
static const void *mem_skip_payload0;
static const void *mem_skip_payload1;
static const void *mem_skip_owner0;
static const void *mem_skip_owner1;

static const char mem_sys_text[] = "SYS used= ";
static const char mem_usr_text[] = "USR used= ";
static const char mem_free_text[] = " free= ";
static const char mem_self_text[] = " self= ";

static void mem_scan_heap(block_t *block)
{
    mem_used = 0u;
    mem_free = 0u;
    mem_self = 0u;

    while (block != 0) {
        uint16_t size = block->size;
        uint8_t self_block =
            (((const void *)block == mem_skip_payload0) ||
             ((const void *)block == mem_skip_payload1) ||
             (block->hdr.owner == mem_skip_owner0) ||
             (block->hdr.owner == mem_skip_owner1));

        if ((block->stat & ALLOCATED) != 0u) {
            if (self_block) {
                mem_self = (uint16_t)(mem_self + size);
            } else {
                mem_used = (uint16_t)(mem_used + size);
            }
        } else {
            mem_free = (uint16_t)(mem_free + size);
        }
        block = (block_t *)app_read_u16(block, BLOCK_NEXT_OFF);
    }
}

int main(int argc, char **argv)
{
    sys_info_t *info;
    process_t *process = 0;
    thread_t *thread = 0;

    (void)argc;
    (void)argv;

    info = app_sys_info();
    if (info != 0) {
        process = app_current_process();
        thread = info->current_thread;

        mem_skip_payload0 = process;
        mem_skip_payload1 = thread;
        mem_skip_owner0 = 0;
        mem_skip_owner1 = 0;
        mem_scan_heap((block_t *)info->system_heap);
        app_write_cstr(mem_sys_text);
        app_write_hex16(mem_used);
        app_write_cstr(mem_free_text);
        app_write_hex16((uint16_t)(mem_free + mem_self));
        if (mem_self != 0u) {
            app_write_cstr(mem_self_text);
            app_write_hex16(mem_self);
        }
        app_write_newline();

        mem_skip_payload0 = 0;
        mem_skip_payload1 = 0;
        mem_skip_owner0 = process;
        mem_skip_owner1 = thread;
        mem_scan_heap((block_t *)info->user_heap);
        app_write_cstr(mem_usr_text);
        app_write_hex16(mem_used);
        app_write_cstr(mem_free_text);
        app_write_hex16((uint16_t)(mem_free + mem_self));
        if (mem_self != 0u) {
            app_write_cstr(mem_self_text);
            app_write_hex16(mem_self);
        }
        app_write_newline();
    }

    return 0;
}
