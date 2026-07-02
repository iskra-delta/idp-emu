            ;; bootstrap.s
            ;;
            ;; minimal app-side bridge for the one operation that cannot be
            ;; reached through `partos_t *` yet: discovering the "partos"
            ;; service table itself through the rst 0x10 service-query hook.
            ;;
            ;; 2026-06-29   tstih
            .module app_bootstrap

            .globl  _app_bind_partos_service
            .globl  _app_dead

            .area   _CODE

_app_bind_partos_service::
            ld      hl,#app_partos_service_name$
            rst     0x10
            ret

_app_dead::
app_dead_loop$:
            halt
            jr      app_dead_loop$

app_partos_service_name$:
            .db     'p','a','r','t','o','s',0
