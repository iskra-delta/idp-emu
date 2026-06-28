#ifndef PARTOS_APP_H
#define PARTOS_APP_H

#include "../../../include/partos.h"
#include <stdint.h>

#define APP_CMDLINE_CAP           64u
#define APP_ARGV_MAX              8u
#define APP_PATH_CAP              64u

/*
 * Compatibility aliases for the older helper layer. New code should prefer the
 * public PartOS names directly, but keeping these aliases makes the migration
 * less disruptive while we move the apps over to normal C entry points.
 */
typedef fat_fs_t pa_fs_t;
typedef fat_file_t pa_file_t;
typedef fat_dirent_t pa_dirent_t;
typedef fat_dirinfo_t pa_dirinfo_t;

partos_t *app_partos(void);
libc_t *app_libc(void);
shell_t *app_shell(void);
const char *app_command_line(void);
const char *app_environment(void);
const char *app_getenv(const char *name);
char *app_current_dir(void);
int app_argc(void);
char **app_argv(void);
process_t *app_current_process(void);
sys_info_t *app_sys_info(void);
fat_fs_t *app_boot_filesystem(void);
void app_bootstrap(void);

int16_t app_clear_screen(void);
int16_t app_write_buffer(const void *buf, uint16_t len);
event_t *app_open_event(void);
event_t *app_event(void);
void app_close_event(void);

void app_exit(void);
void app_write_cstr(const char *s);
void app_write_newline(void);
void app_set_text_attr(uint8_t attr);
void app_write_hex16(uint16_t value);
uint8_t app_format_dir_line(const fat_dirinfo_t *info, char *line);
uint8_t app_read_u8(const void *base, uint8_t offset);
uint16_t app_read_u16(const void *base, uint8_t offset);
void app_write_u16(void *base, uint8_t offset, uint16_t value);
int16_t app_mount_fs(fat_fs_t *fs, const char *dev_name);
int16_t app_lookup_path(fat_fs_t *fs, const char *path, fat_dirent_t *entry);
int16_t app_open_file(fat_fs_t *fs, const char *path, fat_file_t *file);
int16_t app_create_file(fat_fs_t *fs, const char *path, fat_file_t *file);
int16_t app_read_file(fat_file_t *file, void *buf, uint16_t bytes);
int16_t app_write_file(fat_file_t *file, const void *buf, uint16_t bytes);
int16_t app_read_directory(fat_fs_t *fs, const char *path, fat_dirinfo_t *info);
int16_t app_unlink_path(fat_fs_t *fs, const char *path, fat_dirent_t *result);
int16_t app_mkdir_path(fat_fs_t *fs, const char *path, fat_dirent_t *result);
int16_t app_rmdir_path(fat_fs_t *fs, const char *path, fat_dirent_t *result);

char *app_skip_spaces(char *s);
char *app_arg_start(void);
uint8_t app_copy_token(char **cursor, char *dst, uint8_t cap);
uint8_t app_require_eol(const char *cursor);
int16_t app_wait_status(volatile int16_t *status);
uint8_t app_resolve_path(char *dst, uint8_t cap, const char *path);

#define BLOCK_NEXT_OFF             0u
#define PROCESS_NEXT_OFF           0u

/*
 * Low-level bridge retained for the shared runtime/bootstrap layer.
 */
void *pa_init(void);
void pa_dead(void);
int16_t pa_clear_screen(void);
int16_t pa_write_buffer(const void *buf, uint16_t len);
void *pa_query_service(const char *name);
void pa_bind_event(event_t *event);
event_t *pa_create_event(void);
event_t *pa_destroy_event(void);
void pa_wait_one(void);
sys_info_t *pa_get_sys_info(void);
fat_fs_t *pa_get_boot_fs(void);
const char *pa_get_command_line(void);
char *pa_get_current_dir(void);
void pa_exit_process(void);
int16_t pa_set_text_attr(uint8_t attr);

static uint8_t app_file_is_256_aligned(const fat_file_t *file)
{
    return (((uint8_t)(file->size & 0x00ffu)) == 0u) &&
           (((uint8_t)(file->size >> 24)) == 0u);
}

static uint16_t app_file_sector_count(const fat_file_t *file)
{
    return (uint16_t)(file->size >> 8);
}

#endif
