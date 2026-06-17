/*
 * Declares the PartOS "yos" syscall service registration entry point.
 *
 * The service table is intentionally empty for now: the kernel only publishes
 * a stable named service object so future work can start filling the function
 * table without changing the RST 0x10 lookup contract.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#include "service.h"

/*
 * One syscall-table entry. The concrete signatures will be introduced as the
 * kernel grows real syscalls; for now the table is just a zero terminator.
 */
typedef void (*syscall_fn_t)(void);

/*
 * PartOS syscall service name exposed through RST 0x10 lookup.
 */
#define SYSCALL_SERVICE_NAME "yos"

/*
 * Registered kernel service entry and its function table.
 */
extern service_t *syscall_service;
extern syscall_fn_t syscall_table[];

/*
 * Register the PartOS syscall service once. Later calls return the already
 * registered service pointer.
 */
service_t *syscall_init(void);

#endif /* SYSCALL_H */
