/*
 * Declares the PartOS service registry: named tables of syscall function
 * pointers. A service is registered under a name; callers query the service
 * by name to obtain its function table and then index it to reach individual
 * syscalls. The kernel exposes its own calls through the "yos" service, and a
 * process can register services of its own. RST 0x10 is the syscall bridge.
 *
 * MIT License (see: LICENSE)
 * Copyright (C) 2021 tomaz stih
 */
#ifndef SERVICE_H
#define SERVICE_H

#include <stdint.h>

#include "sysobj.h"

/*
 * Maximum service-name length including the terminating NUL.
 */
#define MAX_SVC_NAME_LEN    16

/*
 * Registered named service. Layout must match src/kernel/service.s:
 *   +0 next / +2 owner (sysobj), +4 name[16], +20 fntable.
 */
typedef struct service_s {
    sysobj_t hdr;                       /* service is a system object */
    char     name[MAX_SVC_NAME_LEN];    /* service name */
    void    *fntable;                   /* syscall function table */
} service_t;

/*
 * Head of the global service list.
 */
extern service_t *_svc_first;

/*
 * Register one named service and its function table; returns the new service
 * or NULL on allocation failure.
 */
service_t *svc_register(const char *name, void *fntable);

/*
 * Unregister and destroy one service entry.
 */
service_t *svc_unregister(service_t *s);

/*
 * Look up a registered service's function table by name; NULL if not found.
 */
void *svc_query(const char *name);

/*
 * RST 0x10 syscall bridge around svc_query(): HL = name -> DE = function table.
 */
void svc_query_rst10(void);

#endif /* SERVICE_H */
