/*
 * QEMU monitor
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "monitor-internal.h"
#include "cpu.h"
#include "hw/hw.h"
#include "monitor/qdev.h"
#include "hw/usb.h"
#include "hw/pci/pci.h"
#include "sysemu/watchdog.h"
#include "hw/loader.h"
#include "exec/gdbstub.h"
#include "net/net.h"
#include "net/slirp.h"
#include "chardev/char-mux.h"
#include "ui/qemu-spice.h"
#include "qemu/config-file.h"
#include "qemu/ctype.h"
#include "ui/console.h"
#include "ui/input.h"
#include "audio/audio.h"
#include "disas/disas.h"
#include "sysemu/balloon.h"
#include "qemu/timer.h"
#include "sysemu/hw_accel.h"
#include "authz/list.h"
#include "qapi/util.h"
#include "sysemu/blockdev.h"
#include "sysemu/sysemu.h"
#include "sysemu/tcg.h"
#include "sysemu/tpm.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "trace/control.h"
#include "monitor/hmp-target.h"
#include "monitor/hmp.h"
#ifdef CONFIG_TRACE_SIMPLE
#include "trace/simple.h"
#endif
#include "exec/memory.h"
#include "exec/exec-all.h"
#include "qemu/option.h"
#include "qemu/thread.h"
#include "block/qapi.h"
#include "qapi/qapi-commands.h"
#include "qapi/qapi-emit-events.h"
#include "qapi/error.h"
#include "qapi/qmp-event.h"
#include "qapi/qapi-introspect.h"
#include "sysemu/cpus.h"
#include "qemu/cutils.h"
#include "tcg/tcg.h"

#if defined(TARGET_S390X)
#include "hw/s390x/storage-keys.h"
#include "hw/s390x/storage-attributes.h"
#endif

/* file descriptors passed via SCM_RIGHTS */
typedef struct mon_fd_t mon_fd_t;
struct mon_fd_t {
    char *name;
    int fd;
    QLIST_ENTRY(mon_fd_t) next;
};

/* file descriptor associated with a file descriptor set */
typedef struct MonFdsetFd MonFdsetFd;
struct MonFdsetFd {
    int fd;
    bool removed;
    char *opaque;
    QLIST_ENTRY(MonFdsetFd) next;
};

/* file descriptor set containing fds passed via SCM_RIGHTS */
typedef struct MonFdset MonFdset;
struct MonFdset {
    int64_t id;
    QLIST_HEAD(, MonFdsetFd) fds;
    QLIST_HEAD(, MonFdsetFd) dup_fds;
    QLIST_ENTRY(MonFdset) next;
};

/* QMP checker flags */
#define QMP_ACCEPT_UNKNOWNS 1

/* Protects mon_fdsets */
static QemuMutex mon_fdsets_lock;
static QLIST_HEAD(, MonFdset) mon_fdsets;

static HMPCommand hmp_info_cmds[];

char *qmp_human_monitor_command(const char *command_line, bool has_cpu_index,
                                int64_t cpu_index, Error **errp)
{
    char *output = NULL;
    Monitor *old_mon;
    MonitorHMP hmp = {};

    monitor_data_init(&hmp.common, false, true, false);

    old_mon = cur_mon;
    cur_mon = &hmp.common;

    if (has_cpu_index) {
        int ret = monitor_set_cpu(cpu_index);
        if (ret < 0) {
            cur_mon = old_mon;
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "cpu-index",
                       "a CPU number");
            goto out;
        }
    }

    handle_hmp_command(&hmp, command_line);
    cur_mon = old_mon;

    qemu_mutex_lock(&hmp.common.mon_lock);
    if (qstring_get_length(hmp.common.outbuf) > 0) {
        output = g_strdup(qstring_get_str(hmp.common.outbuf));
    } else {
        output = g_strdup("");
    }
    qemu_mutex_unlock(&hmp.common.mon_lock);

out:
    monitor_data_destroy(&hmp.common);
    return output;
}

/**
 * Is @name in the '|' separated list of names @list?
 */
int hmp_compare_cmd(const char *name, const char *list)
{
    const char *p, *pstart;
    int len;
    len = strlen(name);
    p = list;
    for (;;) {
        pstart = p;
        p = qemu_strchrnul(p, '|');
        if ((p - pstart) == len && !memcmp(pstart, name, len)) {
            return 1;
        }
        if (*p == '\0') {
            break;
        }
        p++;
    }
    return 0;
}

static void do_help_cmd(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, qdict_get_try_str(qdict, "name"));
}

static void hmp_trace_event(Monitor *mon, const QDict *qdict)
{
    const char *tp_name = qdict_get_str(qdict, "name");
    bool new_state = qdict_get_bool(qdict, "option");
    bool has_vcpu = qdict_haskey(qdict, "vcpu");
    int vcpu = qdict_get_try_int(qdict, "vcpu", 0);
    Error *local_err = NULL;

    if (vcpu < 0) {
        monitor_printf(mon, "argument vcpu must be positive");
        return;
    }

    qmp_trace_event_set_state(tp_name, new_state, true, true, has_vcpu, vcpu, &local_err);
    if (local_err) {
        error_report_err(local_err);
    }
}

#ifdef CONFIG_TRACE_SIMPLE
static void hmp_trace_file(Monitor *mon, const QDict *qdict)
{
    const char *op = qdict_get_try_str(qdict, "op");
    const char *arg = qdict_get_try_str(qdict, "arg");

    if (!op) {
        st_print_trace_file_status();
    } else if (!strcmp(op, "on")) {
        st_set_trace_file_enabled(true);
    } else if (!strcmp(op, "off")) {
        st_set_trace_file_enabled(false);
    } else if (!strcmp(op, "flush")) {
        st_flush_trace_buffer();
    } else if (!strcmp(op, "set")) {
        if (arg) {
            st_set_trace_file(arg);
        }
    } else {
        monitor_printf(mon, "unexpected argument \"%s\"\n", op);
        help_cmd(mon, "trace-file");
    }
}
#endif

static void hmp_info_help(Monitor *mon, const QDict *qdict)
{
    help_cmd(mon, "info");
}

static void query_commands_cb(QmpCommand *cmd, void *opaque)
{
    CommandInfoList *info, **list = opaque;

    if (!cmd->enabled) {
        return;
    }

    info = g_malloc0(sizeof(*info));
    info->value = g_malloc0(sizeof(*info->value));
    info->value->name = g_strdup(cmd->name);
    info->next = *list;
    *list = info;
}

CommandInfoList *qmp_query_commands(Error **errp)
{
    CommandInfoList *list = NULL;
    MonitorQMP *mon;

    assert(monitor_is_qmp(cur_mon));
    mon = container_of(cur_mon, MonitorQMP, common);

    qmp_for_each_command(mon->commands, query_commands_cb, &list);

    return list;
}

EventInfoList *qmp_query_events(Error **errp)
{
    /*
     * TODO This deprecated command is the only user of
     * QAPIEvent_str() and QAPIEvent_lookup[].  When the command goes,
     * they should go, too.
     */
    EventInfoList *info, *ev_list = NULL;
    QAPIEvent e;

    for (e = 0 ; e < QAPI_EVENT__MAX ; e++) {
        const char *event_name = QAPIEvent_str(e);
        assert(event_name != NULL);
        info = g_malloc0(sizeof(*info));
        info->value = g_malloc0(sizeof(*info->value));
        info->value->name = g_strdup(event_name);

        info->next = ev_list;
        ev_list = info;
    }

    return ev_list;
}

/*
 * Minor hack: generated marshalling suppressed for this command
 * ('gen': false in the schema) so we can parse the JSON string
 * directly into QObject instead of first parsing it with
 * visit_type_SchemaInfoList() into a SchemaInfoList, then marshal it
 * to QObject with generated output marshallers, every time.  Instead,
 * we do it in test-qobject-input-visitor.c, just to make sure
 * qapi-gen.py's output actually conforms to the schema.
 */
static void qmp_query_qmp_schema(QDict *qdict, QObject **ret_data,
                                 Error **errp)
{
    *ret_data = qobject_from_qlit(&qmp_schema_qlit);
}

static void monitor_init_qmp_commands(void)
{
    /*
     * Two command lists:
     * - qmp_commands contains all QMP commands
     * - qmp_cap_negotiation_commands contains just
     *   "qmp_capabilities", to enforce capability negotiation
     */

    qmp_init_marshal(&qmp_commands);

    qmp_register_command(&qmp_commands, "query-qmp-schema",
                         qmp_query_qmp_schema, QCO_ALLOW_PRECONFIG);
    qmp_register_command(&qmp_commands, "device_add", qmp_device_add,
                         QCO_NO_OPTIONS);
    qmp_register_command(&qmp_commands, "netdev_add", qmp_netdev_add,
                         QCO_NO_OPTIONS);

    QTAILQ_INIT(&qmp_cap_negotiation_commands);
    qmp_register_command(&qmp_cap_negotiation_commands, "qmp_capabilities",
                         qmp_marshal_qmp_capabilities, QCO_ALLOW_PRECONFIG);
}

/*
 * Accept QMP capabilities in @list for @mon.
 * On success, set mon->qmp.capab[], and return true.
 * On error, set @errp, and return false.
 */
static bool qmp_caps_accept(MonitorQMP *mon, QMPCapabilityList *list,
                            Error **errp)
{
    GString *unavailable = NULL;
    bool capab[QMP_CAPABILITY__MAX];

    memset(capab, 0, sizeof(capab));

    for (; list; list = list->next) {
        if (!mon->capab_offered[list->value]) {
            if (!unavailable) {
                unavailable = g_string_new(QMPCapability_str(list->value));
            } else {
                g_string_append_printf(unavailable, ", %s",
                                      QMPCapability_str(list->value));
            }
        }
        capab[list->value] = true;
    }

    if (unavailable) {
        error_setg(errp, "Capability %s not available", unavailable->str);
        g_string_free(unavailable, true);
        return false;
    }

    memcpy(mon->capab, capab, sizeof(capab));
    return true;
}

void qmp_qmp_capabilities(bool has_enable, QMPCapabilityList *enable,
                          Error **errp)
{
    MonitorQMP *mon;

    assert(monitor_is_qmp(cur_mon));
    mon = container_of(cur_mon, MonitorQMP, common);

    if (mon->commands == &qmp_commands) {
        error_set(errp, ERROR_CLASS_COMMAND_NOT_FOUND,
                  "Capabilities negotiation is already complete, command "
                  "ignored");
        return;
    }

    if (!qmp_caps_accept(mon, enable, errp)) {
        return;
    }

    mon->commands = &qmp_commands;
}

/* Set the current CPU defined by the user. Callers must hold BQL. */
int monitor_set_cpu(int cpu_index)
{
    CPUState *cpu;

    cpu = qemu_get_cpu(cpu_index);
    if (cpu == NULL) {
        return -1;
    }
    g_free(cur_mon->mon_cpu_path);
    cur_mon->mon_cpu_path = object_get_canonical_path(OBJECT(cpu));
    return 0;
}

/* Callers must hold BQL. */
static CPUState *mon_get_cpu_sync(bool synchronize)
{
    CPUState *cpu;

    if (cur_mon->mon_cpu_path) {
        cpu = (CPUState *) object_resolve_path_type(cur_mon->mon_cpu_path,
                                                    TYPE_CPU, NULL);
        if (!cpu) {
            g_free(cur_mon->mon_cpu_path);
            cur_mon->mon_cpu_path = NULL;
        }
    }
    if (!cur_mon->mon_cpu_path) {
        if (!first_cpu) {
            return NULL;
        }
        monitor_set_cpu(first_cpu->cpu_index);
        cpu = first_cpu;
    }
    if (synchronize) {
        cpu_synchronize_state(cpu);
    }
    return cpu;
}

CPUState *mon_get_cpu(void)
{
    return mon_get_cpu_sync(true);
}

CPUArchState *mon_get_cpu_env(void)
{
    CPUState *cs = mon_get_cpu();

    return cs ? cs->env_ptr : NULL;
}

int monitor_get_cpu_index(void)
{
    CPUState *cs = mon_get_cpu_sync(false);

    return cs ? cs->cpu_index : UNASSIGNED_CPU_INDEX;
}

static void hmp_info_registers(Monitor *mon, const QDict *qdict)
{
    bool all_cpus = qdict_get_try_bool(qdict, "cpustate_all", false);
    CPUState *cs;

    if (all_cpus) {
        CPU_FOREACH(cs) {
            monitor_printf(mon, "\nCPU#%d\n", cs->cpu_index);
            cpu_dump_state(cs, NULL, CPU_DUMP_FPU);
        }
    } else {
        cs = mon_get_cpu();

        if (!cs) {
            monitor_printf(mon, "No CPU available\n");
            return;
        }

        cpu_dump_state(cs, NULL, CPU_DUMP_FPU);
    }
}

#ifdef CONFIG_TCG
static void hmp_info_jit(Monitor *mon, const QDict *qdict)
{
    if (!tcg_enabled()) {
        error_report("JIT information is only available with accel=tcg");
        return;
    }

    dump_exec_info();
    dump_drift_info();
}

static void hmp_info_opcount(Monitor *mon, const QDict *qdict)
{
    dump_opcount_info();
}
#endif

static void hmp_info_sync_profile(Monitor *mon, const QDict *qdict)
{
    int64_t max = qdict_get_try_int(qdict, "max", 10);
    bool mean = qdict_get_try_bool(qdict, "mean", false);
    bool coalesce = !qdict_get_try_bool(qdict, "no_coalesce", false);
    enum QSPSortBy sort_by;

    sort_by = mean ? QSP_SORT_BY_AVG_WAIT_TIME : QSP_SORT_BY_TOTAL_WAIT_TIME;
    qsp_report(max, sort_by, coalesce);
}

static void hmp_info_history(Monitor *mon, const QDict *qdict)
{
    MonitorHMP *hmp_mon = container_of(mon, MonitorHMP, common);
    int i;
    const char *str;

    if (!hmp_mon->rs) {
        return;
    }
    i = 0;
    for(;;) {
        str = readline_get_history(hmp_mon->rs, i);
        if (!str) {
            break;
        }
        monitor_printf(mon, "%d: '%s'\n", i, str);
        i++;
    }
}

static void hmp_info_cpustats(Monitor *mon, const QDict *qdict)
{
    CPUState *cs = mon_get_cpu();

    if (!cs) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }
    cpu_dump_statistics(cs, 0);
}

static void hmp_info_trace_events(Monitor *mon, const QDict *qdict)
{
    const char *name = qdict_get_try_str(qdict, "name");
    bool has_vcpu = qdict_haskey(qdict, "vcpu");
    int vcpu = qdict_get_try_int(qdict, "vcpu", 0);
    TraceEventInfoList *events;
    TraceEventInfoList *elem;
    Error *local_err = NULL;

    if (name == NULL) {
        name = "*";
    }
    if (vcpu < 0) {
        monitor_printf(mon, "argument vcpu must be positive");
        return;
    }

    events = qmp_trace_event_get_state(name, has_vcpu, vcpu, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    for (elem = events; elem != NULL; elem = elem->next) {
        monitor_printf(mon, "%s : state %u\n",
                       elem->value->name,
                       elem->value->state == TRACE_EVENT_STATE_ENABLED ? 1 : 0);
    }
    qapi_free_TraceEventInfoList(events);
}

void qmp_client_migrate_info(const char *protocol, const char *hostname,
                             bool has_port, int64_t port,
                             bool has_tls_port, int64_t tls_port,
                             bool has_cert_subject, const char *cert_subject,
                             Error **errp)
{
    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }

        if (!has_port && !has_tls_port) {
            error_setg(errp, QERR_MISSING_PARAMETER, "port/tls-port");
            return;
        }

        if (qemu_spice_migrate_info(hostname,
                                    has_port ? port : -1,
                                    has_tls_port ? tls_port : -1,
                                    cert_subject)) {
            error_setg(errp, QERR_UNDEFINED_ERROR);
            return;
        }
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "protocol", "spice");
}

static void hmp_logfile(Monitor *mon, const QDict *qdict)
{
    Error *err = NULL;

    qemu_set_log_filename(qdict_get_str(qdict, "filename"), &err);
    if (err) {
        error_report_err(err);
    }
}

static void hmp_log(Monitor *mon, const QDict *qdict)
{
    int mask;
    const char *items = qdict_get_str(qdict, "items");

    if (!strcmp(items, "none")) {
        mask = 0;
    } else {
        mask = qemu_str_to_log_mask(items);
        if (!mask) {
            help_cmd(mon, "log");
            return;
        }
    }
    qemu_set_log(mask);
}

static void hmp_singlestep(Monitor *mon, const QDict *qdict)
{
    const char *option = qdict_get_try_str(qdict, "option");
    if (!option || !strcmp(option, "on")) {
        singlestep = 1;
    } else if (!strcmp(option, "off")) {
        singlestep = 0;
    } else {
        monitor_printf(mon, "unexpected option %s\n", option);
    }
}

static void hmp_gdbserver(Monitor *mon, const QDict *qdict)
{
    const char *device = qdict_get_try_str(qdict, "device");
    if (!device)
        device = "tcp::" DEFAULT_GDBSTUB_PORT;
    if (gdbserver_start(device) < 0) {
        monitor_printf(mon, "Could not open gdbserver on device '%s'\n",
                       device);
    } else if (strcmp(device, "none") == 0) {
        monitor_printf(mon, "Disabled gdbserver\n");
    } else {
        monitor_printf(mon, "Waiting for gdb connection on device '%s'\n",
                       device);
    }
}

static void hmp_watchdog_action(Monitor *mon, const QDict *qdict)
{
    const char *action = qdict_get_str(qdict, "action");
    if (select_watchdog_action(action) == -1) {
        monitor_printf(mon, "Unknown watchdog action '%s'\n", action);
    }
}

static void monitor_printc(Monitor *mon, int c)
{
    monitor_printf(mon, "'");
    switch(c) {
    case '\'':
        monitor_printf(mon, "\\'");
        break;
    case '\\':
        monitor_printf(mon, "\\\\");
        break;
    case '\n':
        monitor_printf(mon, "\\n");
        break;
    case '\r':
        monitor_printf(mon, "\\r");
        break;
    default:
        if (c >= 32 && c <= 126) {
            monitor_printf(mon, "%c", c);
        } else {
            monitor_printf(mon, "\\x%02x", c);
        }
        break;
    }
    monitor_printf(mon, "'");
}

static void memory_dump(Monitor *mon, int count, int format, int wsize,
                        hwaddr addr, int is_physical)
{
    int l, line_size, i, max_digits, len;
    uint8_t buf[16];
    uint64_t v;
    CPUState *cs = mon_get_cpu();

    if (!cs && (format == 'i' || !is_physical)) {
        monitor_printf(mon, "Can not dump without CPU\n");
        return;
    }

    if (format == 'i') {
        monitor_disas(mon, cs, addr, count, is_physical);
        return;
    }

    len = wsize * count;
    if (wsize == 1)
        line_size = 8;
    else
        line_size = 16;
    max_digits = 0;

    switch(format) {
    case 'o':
        max_digits = DIV_ROUND_UP(wsize * 8, 3);
        break;
    default:
    case 'x':
        max_digits = (wsize * 8) / 4;
        break;
    case 'u':
    case 'd':
        max_digits = DIV_ROUND_UP(wsize * 8 * 10, 33);
        break;
    case 'c':
        wsize = 1;
        break;
    }

    while (len > 0) {
        if (is_physical)
            monitor_printf(mon, TARGET_FMT_plx ":", addr);
        else
            monitor_printf(mon, TARGET_FMT_lx ":", (target_ulong)addr);
        l = len;
        if (l > line_size)
            l = line_size;
        if (is_physical) {
            AddressSpace *as = cs ? cs->as : &address_space_memory;
            MemTxResult r = address_space_read(as, addr,
                                               MEMTXATTRS_UNSPECIFIED, buf, l);
            if (r != MEMTX_OK) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        } else {
            if (cpu_memory_rw_debug(cs, addr, buf, l, 0) < 0) {
                monitor_printf(mon, " Cannot access memory\n");
                break;
            }
        }
        i = 0;
        while (i < l) {
            switch(wsize) {
            default:
            case 1:
                v = ldub_p(buf + i);
                break;
            case 2:
                v = lduw_p(buf + i);
                break;
            case 4:
                v = (uint32_t)ldl_p(buf + i);
                break;
            case 8:
                v = ldq_p(buf + i);
                break;
            }
            monitor_printf(mon, " ");
            switch(format) {
            case 'o':
                monitor_printf(mon, "%#*" PRIo64, max_digits, v);
                break;
            case 'x':
                monitor_printf(mon, "0x%0*" PRIx64, max_digits, v);
                break;
            case 'u':
                monitor_printf(mon, "%*" PRIu64, max_digits, v);
                break;
            case 'd':
                monitor_printf(mon, "%*" PRId64, max_digits, v);
                break;
            case 'c':
                monitor_printc(mon, v);
                break;
            }
            i += wsize;
        }
        monitor_printf(mon, "\n");
        addr += l;
        len -= l;
    }
}

static void hmp_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    target_long addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 0);
}

static void hmp_physical_memory_dump(Monitor *mon, const QDict *qdict)
{
    int count = qdict_get_int(qdict, "count");
    int format = qdict_get_int(qdict, "format");
    int size = qdict_get_int(qdict, "size");
    hwaddr addr = qdict_get_int(qdict, "addr");

    memory_dump(mon, count, format, size, addr, 1);
}

static void *gpa2hva(MemoryRegion **p_mr, hwaddr addr, Error **errp)
{
    MemoryRegionSection mrs = memory_region_find(get_system_memory(),
                                                 addr, 1);

    if (!mrs.mr) {
        error_setg(errp, "No memory is mapped at address 0x%" HWADDR_PRIx, addr);
        return NULL;
    }

    if (!memory_region_is_ram(mrs.mr) && !memory_region_is_romd(mrs.mr)) {
        error_setg(errp, "Memory at address 0x%" HWADDR_PRIx "is not RAM", addr);
        memory_region_unref(mrs.mr);
        return NULL;
    }

    *p_mr = mrs.mr;
    return qemu_map_ram_ptr(mrs.mr->ram_block, mrs.offset_within_region);
}

static void hmp_gpa2hva(Monitor *mon, const QDict *qdict)
{
    hwaddr addr = qdict_get_int(qdict, "addr");
    Error *local_err = NULL;
    MemoryRegion *mr = NULL;
    void *ptr;

    ptr = gpa2hva(&mr, addr, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    monitor_printf(mon, "Host virtual address for 0x%" HWADDR_PRIx
                   " (%s) is %p\n",
                   addr, mr->name, ptr);

    memory_region_unref(mr);
}

static void hmp_gva2gpa(Monitor *mon, const QDict *qdict)
{
    target_ulong addr = qdict_get_int(qdict, "addr");
    MemTxAttrs attrs;
    CPUState *cs = mon_get_cpu();
    hwaddr gpa;

    if (!cs) {
        monitor_printf(mon, "No cpu\n");
        return;
    }

    gpa  = cpu_get_phys_page_attrs_debug(cs, addr & TARGET_PAGE_MASK, &attrs);
    if (gpa == -1) {
        monitor_printf(mon, "Unmapped\n");
    } else {
        monitor_printf(mon, "gpa: %#" HWADDR_PRIx "\n",
                       gpa + (addr & ~TARGET_PAGE_MASK));
    }
}

#ifdef CONFIG_LINUX
static uint64_t vtop(void *ptr, Error **errp)
{
    uint64_t pinfo;
    uint64_t ret = -1;
    uintptr_t addr = (uintptr_t) ptr;
    uintptr_t pagesize = getpagesize();
    off_t offset = addr / pagesize * sizeof(pinfo);
    int fd;

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd == -1) {
        error_setg_errno(errp, errno, "Cannot open /proc/self/pagemap");
        return -1;
    }

    /* Force copy-on-write if necessary.  */
    atomic_add((uint8_t *)ptr, 0);

    if (pread(fd, &pinfo, sizeof(pinfo), offset) != sizeof(pinfo)) {
        error_setg_errno(errp, errno, "Cannot read pagemap");
        goto out;
    }
    if ((pinfo & (1ull << 63)) == 0) {
        error_setg(errp, "Page not present");
        goto out;
    }
    ret = ((pinfo & 0x007fffffffffffffull) * pagesize) | (addr & (pagesize - 1));

out:
    close(fd);
    return ret;
}

static void hmp_gpa2hpa(Monitor *mon, const QDict *qdict)
{
    hwaddr addr = qdict_get_int(qdict, "addr");
    Error *local_err = NULL;
    MemoryRegion *mr = NULL;
    void *ptr;
    uint64_t physaddr;

    ptr = gpa2hva(&mr, addr, &local_err);
    if (local_err) {
        error_report_err(local_err);
        return;
    }

    physaddr = vtop(ptr, &local_err);
    if (local_err) {
        error_report_err(local_err);
    } else {
        monitor_printf(mon, "Host physical address for 0x%" HWADDR_PRIx
                       " (%s) is 0x%" PRIx64 "\n",
                       addr, mr->name, (uint64_t) physaddr);
    }

    memory_region_unref(mr);
}
#endif

static void do_print(Monitor *mon, const QDict *qdict)
{
    int format = qdict_get_int(qdict, "format");
    hwaddr val = qdict_get_int(qdict, "val");

    switch(format) {
    case 'o':
        monitor_printf(mon, "%#" HWADDR_PRIo, val);
        break;
    case 'x':
        monitor_printf(mon, "%#" HWADDR_PRIx, val);
        break;
    case 'u':
        monitor_printf(mon, "%" HWADDR_PRIu, val);
        break;
    default:
    case 'd':
        monitor_printf(mon, "%" HWADDR_PRId, val);
        break;
    case 'c':
        monitor_printc(mon, val);
        break;
    }
    monitor_printf(mon, "\n");
}

static void hmp_sum(Monitor *mon, const QDict *qdict)
{
    uint32_t addr;
    uint16_t sum;
    uint32_t start = qdict_get_int(qdict, "start");
    uint32_t size = qdict_get_int(qdict, "size");

    sum = 0;
    for(addr = start; addr < (start + size); addr++) {
        uint8_t val = address_space_ldub(&address_space_memory, addr,
                                         MEMTXATTRS_UNSPECIFIED, NULL);
        /* BSD sum algorithm ('sum' Unix command) */
        sum = (sum >> 1) | (sum << 15);
        sum += val;
    }
    monitor_printf(mon, "%05d\n", sum);
}

static int mouse_button_state;

static void hmp_mouse_move(Monitor *mon, const QDict *qdict)
{
    int dx, dy, dz, button;
    const char *dx_str = qdict_get_str(qdict, "dx_str");
    const char *dy_str = qdict_get_str(qdict, "dy_str");
    const char *dz_str = qdict_get_try_str(qdict, "dz_str");

    dx = strtol(dx_str, NULL, 0);
    dy = strtol(dy_str, NULL, 0);
    qemu_input_queue_rel(NULL, INPUT_AXIS_X, dx);
    qemu_input_queue_rel(NULL, INPUT_AXIS_Y, dy);

    if (dz_str) {
        dz = strtol(dz_str, NULL, 0);
        if (dz != 0) {
            button = (dz > 0) ? INPUT_BUTTON_WHEEL_UP : INPUT_BUTTON_WHEEL_DOWN;
            qemu_input_queue_btn(NULL, button, true);
            qemu_input_event_sync();
            qemu_input_queue_btn(NULL, button, false);
        }
    }
    qemu_input_event_sync();
}

static void hmp_mouse_button(Monitor *mon, const QDict *qdict)
{
    static uint32_t bmap[INPUT_BUTTON__MAX] = {
        [INPUT_BUTTON_LEFT]       = MOUSE_EVENT_LBUTTON,
        [INPUT_BUTTON_MIDDLE]     = MOUSE_EVENT_MBUTTON,
        [INPUT_BUTTON_RIGHT]      = MOUSE_EVENT_RBUTTON,
    };
    int button_state = qdict_get_int(qdict, "button_state");

    if (mouse_button_state == button_state) {
        return;
    }
    qemu_input_update_buttons(NULL, bmap, mouse_button_state, button_state);
    qemu_input_event_sync();
    mouse_button_state = button_state;
}

static void hmp_ioport_read(Monitor *mon, const QDict *qdict)
{
    int size = qdict_get_int(qdict, "size");
    int addr = qdict_get_int(qdict, "addr");
    int has_index = qdict_haskey(qdict, "index");
    uint32_t val;
    int suffix;

    if (has_index) {
        int index = qdict_get_int(qdict, "index");
        cpu_outb(addr & IOPORTS_MASK, index & 0xff);
        addr++;
    }
    addr &= 0xffff;

    switch(size) {
    default:
    case 1:
        val = cpu_inb(addr);
        suffix = 'b';
        break;
    case 2:
        val = cpu_inw(addr);
        suffix = 'w';
        break;
    case 4:
        val = cpu_inl(addr);
        suffix = 'l';
        break;
    }
    monitor_printf(mon, "port%c[0x%04x] = %#0*x\n",
                   suffix, addr, size * 2, val);
}

static void hmp_ioport_write(Monitor *mon, const QDict *qdict)
{
    int size = qdict_get_int(qdict, "size");
    int addr = qdict_get_int(qdict, "addr");
    int val = qdict_get_int(qdict, "val");

    addr &= IOPORTS_MASK;

    switch (size) {
    default:
    case 1:
        cpu_outb(addr, val);
        break;
    case 2:
        cpu_outw(addr, val);
        break;
    case 4:
        cpu_outl(addr, val);
        break;
    }
}

static void hmp_boot_set(Monitor *mon, const QDict *qdict)
{
    Error *local_err = NULL;
    const char *bootdevice = qdict_get_str(qdict, "bootdevice");

    qemu_boot_set(bootdevice, &local_err);
    if (local_err) {
        error_report_err(local_err);
    } else {
        monitor_printf(mon, "boot device list now set to %s\n", bootdevice);
    }
}

static void hmp_info_mtree(Monitor *mon, const QDict *qdict)
{
    bool flatview = qdict_get_try_bool(qdict, "flatview", false);
    bool dispatch_tree = qdict_get_try_bool(qdict, "dispatch_tree", false);
    bool owner = qdict_get_try_bool(qdict, "owner", false);

    mtree_info(flatview, dispatch_tree, owner);
}

#ifdef CONFIG_PROFILER

int64_t dev_time;

static void hmp_info_profile(Monitor *mon, const QDict *qdict)
{
    static int64_t last_cpu_exec_time;
    int64_t cpu_exec_time;
    int64_t delta;

    cpu_exec_time = tcg_cpu_exec_time();
    delta = cpu_exec_time - last_cpu_exec_time;

    monitor_printf(mon, "async time  %" PRId64 " (%0.3f)\n",
                   dev_time, dev_time / (double)NANOSECONDS_PER_SECOND);
    monitor_printf(mon, "qemu time   %" PRId64 " (%0.3f)\n",
                   delta, delta / (double)NANOSECONDS_PER_SECOND);
    last_cpu_exec_time = cpu_exec_time;
    dev_time = 0;
}
#else
static void hmp_info_profile(Monitor *mon, const QDict *qdict)
{
    monitor_printf(mon, "Internal profiler not compiled\n");
}
#endif

/* Capture support */
static QLIST_HEAD (capture_list_head, CaptureState) capture_head;

static void hmp_info_capture(Monitor *mon, const QDict *qdict)
{
    int i;
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        monitor_printf(mon, "[%d]: ", i);
        s->ops.info (s->opaque);
    }
}

static void hmp_stopcapture(Monitor *mon, const QDict *qdict)
{
    int i;
    int n = qdict_get_int(qdict, "n");
    CaptureState *s;

    for (s = capture_head.lh_first, i = 0; s; s = s->entries.le_next, ++i) {
        if (i == n) {
            s->ops.destroy (s->opaque);
            QLIST_REMOVE (s, entries);
            g_free (s);
            return;
        }
    }
}

static void hmp_wavcapture(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    int has_freq = qdict_haskey(qdict, "freq");
    int freq = qdict_get_try_int(qdict, "freq", -1);
    int has_bits = qdict_haskey(qdict, "bits");
    int bits = qdict_get_try_int(qdict, "bits", -1);
    int has_channels = qdict_haskey(qdict, "nchannels");
    int nchannels = qdict_get_try_int(qdict, "nchannels", -1);
    CaptureState *s;

    s = g_malloc0 (sizeof (*s));

    freq = has_freq ? freq : 44100;
    bits = has_bits ? bits : 16;
    nchannels = has_channels ? nchannels : 2;

    if (wav_start_capture (s, path, freq, bits, nchannels)) {
        monitor_printf(mon, "Failed to add wave capture\n");
        g_free (s);
        return;
    }
    QLIST_INSERT_HEAD (&capture_head, s, entries);
}

static QAuthZList *find_auth(Monitor *mon, const char *name)
{
    Object *obj;
    Object *container;

    container = object_get_objects_root();
    obj = object_resolve_path_component(container, name);
    if (!obj) {
        monitor_printf(mon, "acl: unknown list '%s'\n", name);
        return NULL;
    }

    return QAUTHZ_LIST(obj);
}

static bool warn_acl;
static void hmp_warn_acl(void)
{
    if (warn_acl) {
        return;
    }
    error_report("The acl_show, acl_reset, acl_policy, acl_add, acl_remove "
                 "commands are deprecated with no replacement. Authorization "
                 "for VNC should be performed using the pluggable QAuthZ "
                 "objects");
    warn_acl = true;
}

static void hmp_acl_show(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    QAuthZList *auth = find_auth(mon, aclname);
    QAuthZListRuleList *rules;
    size_t i = 0;

    hmp_warn_acl();

    if (!auth) {
        return;
    }

    monitor_printf(mon, "policy: %s\n",
                   QAuthZListPolicy_str(auth->policy));

    rules = auth->rules;
    while (rules) {
        QAuthZListRule *rule = rules->value;
        i++;
        monitor_printf(mon, "%zu: %s %s\n", i,
                       QAuthZListPolicy_str(rule->policy),
                       rule->match);
        rules = rules->next;
    }
}

static void hmp_acl_reset(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    QAuthZList *auth = find_auth(mon, aclname);

    hmp_warn_acl();

    if (!auth) {
        return;
    }

    auth->policy = QAUTHZ_LIST_POLICY_DENY;
    qapi_free_QAuthZListRuleList(auth->rules);
    auth->rules = NULL;
    monitor_printf(mon, "acl: removed all rules\n");
}

static void hmp_acl_policy(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    const char *policy = qdict_get_str(qdict, "policy");
    QAuthZList *auth = find_auth(mon, aclname);
    int val;
    Error *err = NULL;

    hmp_warn_acl();

    if (!auth) {
        return;
    }

    val = qapi_enum_parse(&QAuthZListPolicy_lookup,
                          policy,
                          QAUTHZ_LIST_POLICY_DENY,
                          &err);
    if (err) {
        error_free(err);
        monitor_printf(mon, "acl: unknown policy '%s', "
                       "expected 'deny' or 'allow'\n", policy);
    } else {
        auth->policy = val;
        if (auth->policy == QAUTHZ_LIST_POLICY_ALLOW) {
            monitor_printf(mon, "acl: policy set to 'allow'\n");
        } else {
            monitor_printf(mon, "acl: policy set to 'deny'\n");
        }
    }
}

static QAuthZListFormat hmp_acl_get_format(const char *match)
{
    if (strchr(match, '*')) {
        return QAUTHZ_LIST_FORMAT_GLOB;
    } else {
        return QAUTHZ_LIST_FORMAT_EXACT;
    }
}

static void hmp_acl_add(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    const char *match = qdict_get_str(qdict, "match");
    const char *policystr = qdict_get_str(qdict, "policy");
    int has_index = qdict_haskey(qdict, "index");
    int index = qdict_get_try_int(qdict, "index", -1);
    QAuthZList *auth = find_auth(mon, aclname);
    Error *err = NULL;
    QAuthZListPolicy policy;
    QAuthZListFormat format;
    size_t i = 0;

    hmp_warn_acl();

    if (!auth) {
        return;
    }

    policy = qapi_enum_parse(&QAuthZListPolicy_lookup,
                             policystr,
                             QAUTHZ_LIST_POLICY_DENY,
                             &err);
    if (err) {
        error_free(err);
        monitor_printf(mon, "acl: unknown policy '%s', "
                       "expected 'deny' or 'allow'\n", policystr);
        return;
    }

    format = hmp_acl_get_format(match);

    if (has_index && index == 0) {
        monitor_printf(mon, "acl: unable to add acl entry\n");
        return;
    }

    if (has_index) {
        i = qauthz_list_insert_rule(auth, match, policy,
                                    format, index - 1, &err);
    } else {
        i = qauthz_list_append_rule(auth, match, policy,
                                    format, &err);
    }
    if (err) {
        monitor_printf(mon, "acl: unable to add rule: %s",
                       error_get_pretty(err));
        error_free(err);
    } else {
        monitor_printf(mon, "acl: added rule at position %zu\n", i + 1);
    }
}

static void hmp_acl_remove(Monitor *mon, const QDict *qdict)
{
    const char *aclname = qdict_get_str(qdict, "aclname");
    const char *match = qdict_get_str(qdict, "match");
    QAuthZList *auth = find_auth(mon, aclname);
    ssize_t i = 0;

    hmp_warn_acl();

    if (!auth) {
        return;
    }

    i = qauthz_list_delete_rule(auth, match);
    if (i >= 0) {
        monitor_printf(mon, "acl: removed rule at position %zu\n", i + 1);
    } else {
        monitor_printf(mon, "acl: no matching acl entry\n");
    }
}

void qmp_getfd(const char *fdname, Error **errp)
{
    mon_fd_t *monfd;
    int fd, tmp_fd;

    fd = qemu_chr_fe_get_msgfd(&cur_mon->chr);
    if (fd == -1) {
        error_setg(errp, QERR_FD_NOT_SUPPLIED);
        return;
    }

    if (qemu_isdigit(fdname[0])) {
        close(fd);
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdname",
                   "a name not starting with a digit");
        return;
    }

    qemu_mutex_lock(&cur_mon->mon_lock);
    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        tmp_fd = monfd->fd;
        monfd->fd = fd;
        qemu_mutex_unlock(&cur_mon->mon_lock);
        /* Make sure close() is outside critical section */
        close(tmp_fd);
        return;
    }

    monfd = g_malloc0(sizeof(mon_fd_t));
    monfd->name = g_strdup(fdname);
    monfd->fd = fd;

    QLIST_INSERT_HEAD(&cur_mon->fds, monfd, next);
    qemu_mutex_unlock(&cur_mon->mon_lock);
}

void qmp_closefd(const char *fdname, Error **errp)
{
    mon_fd_t *monfd;
    int tmp_fd;

    qemu_mutex_lock(&cur_mon->mon_lock);
    QLIST_FOREACH(monfd, &cur_mon->fds, next) {
        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        QLIST_REMOVE(monfd, next);
        tmp_fd = monfd->fd;
        g_free(monfd->name);
        g_free(monfd);
        qemu_mutex_unlock(&cur_mon->mon_lock);
        /* Make sure close() is outside critical section */
        close(tmp_fd);
        return;
    }

    qemu_mutex_unlock(&cur_mon->mon_lock);
    error_setg(errp, QERR_FD_NOT_FOUND, fdname);
}

int monitor_get_fd(Monitor *mon, const char *fdname, Error **errp)
{
    mon_fd_t *monfd;

    qemu_mutex_lock(&mon->mon_lock);
    QLIST_FOREACH(monfd, &mon->fds, next) {
        int fd;

        if (strcmp(monfd->name, fdname) != 0) {
            continue;
        }

        fd = monfd->fd;

        /* caller takes ownership of fd */
        QLIST_REMOVE(monfd, next);
        g_free(monfd->name);
        g_free(monfd);
        qemu_mutex_unlock(&mon->mon_lock);

        return fd;
    }

    qemu_mutex_unlock(&mon->mon_lock);
    error_setg(errp, "File descriptor named '%s' has not been found", fdname);
    return -1;
}

static void monitor_fdset_cleanup(MonFdset *mon_fdset)
{
    MonFdsetFd *mon_fdset_fd;
    MonFdsetFd *mon_fdset_fd_next;

    QLIST_FOREACH_SAFE(mon_fdset_fd, &mon_fdset->fds, next, mon_fdset_fd_next) {
        if ((mon_fdset_fd->removed ||
                (QLIST_EMPTY(&mon_fdset->dup_fds) && mon_refcount == 0)) &&
                runstate_is_running()) {
            close(mon_fdset_fd->fd);
            g_free(mon_fdset_fd->opaque);
            QLIST_REMOVE(mon_fdset_fd, next);
            g_free(mon_fdset_fd);
        }
    }

    if (QLIST_EMPTY(&mon_fdset->fds) && QLIST_EMPTY(&mon_fdset->dup_fds)) {
        QLIST_REMOVE(mon_fdset, next);
        g_free(mon_fdset);
    }
}

void monitor_fdsets_cleanup(void)
{
    MonFdset *mon_fdset;
    MonFdset *mon_fdset_next;

    qemu_mutex_lock(&mon_fdsets_lock);
    QLIST_FOREACH_SAFE(mon_fdset, &mon_fdsets, next, mon_fdset_next) {
        monitor_fdset_cleanup(mon_fdset);
    }
    qemu_mutex_unlock(&mon_fdsets_lock);
}

AddfdInfo *qmp_add_fd(bool has_fdset_id, int64_t fdset_id, bool has_opaque,
                      const char *opaque, Error **errp)
{
    int fd;
    Monitor *mon = cur_mon;
    AddfdInfo *fdinfo;

    fd = qemu_chr_fe_get_msgfd(&mon->chr);
    if (fd == -1) {
        error_setg(errp, QERR_FD_NOT_SUPPLIED);
        goto error;
    }

    fdinfo = monitor_fdset_add_fd(fd, has_fdset_id, fdset_id,
                                  has_opaque, opaque, errp);
    if (fdinfo) {
        return fdinfo;
    }

error:
    if (fd != -1) {
        close(fd);
    }
    return NULL;
}

void qmp_remove_fd(int64_t fdset_id, bool has_fd, int64_t fd, Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    char fd_str[60];

    qemu_mutex_lock(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            if (has_fd) {
                if (mon_fdset_fd->fd != fd) {
                    continue;
                }
                mon_fdset_fd->removed = true;
                break;
            } else {
                mon_fdset_fd->removed = true;
            }
        }
        if (has_fd && !mon_fdset_fd) {
            goto error;
        }
        monitor_fdset_cleanup(mon_fdset);
        qemu_mutex_unlock(&mon_fdsets_lock);
        return;
    }

error:
    qemu_mutex_unlock(&mon_fdsets_lock);
    if (has_fd) {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64 ", fd:%" PRId64,
                 fdset_id, fd);
    } else {
        snprintf(fd_str, sizeof(fd_str), "fdset-id:%" PRId64, fdset_id);
    }
    error_setg(errp, QERR_FD_NOT_FOUND, fd_str);
}

FdsetInfoList *qmp_query_fdsets(Error **errp)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    FdsetInfoList *fdset_list = NULL;

    qemu_mutex_lock(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        FdsetInfoList *fdset_info = g_malloc0(sizeof(*fdset_info));
        FdsetFdInfoList *fdsetfd_list = NULL;

        fdset_info->value = g_malloc0(sizeof(*fdset_info->value));
        fdset_info->value->fdset_id = mon_fdset->id;

        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            FdsetFdInfoList *fdsetfd_info;

            fdsetfd_info = g_malloc0(sizeof(*fdsetfd_info));
            fdsetfd_info->value = g_malloc0(sizeof(*fdsetfd_info->value));
            fdsetfd_info->value->fd = mon_fdset_fd->fd;
            if (mon_fdset_fd->opaque) {
                fdsetfd_info->value->has_opaque = true;
                fdsetfd_info->value->opaque = g_strdup(mon_fdset_fd->opaque);
            } else {
                fdsetfd_info->value->has_opaque = false;
            }

            fdsetfd_info->next = fdsetfd_list;
            fdsetfd_list = fdsetfd_info;
        }

        fdset_info->value->fds = fdsetfd_list;

        fdset_info->next = fdset_list;
        fdset_list = fdset_info;
    }
    qemu_mutex_unlock(&mon_fdsets_lock);

    return fdset_list;
}

AddfdInfo *monitor_fdset_add_fd(int fd, bool has_fdset_id, int64_t fdset_id,
                                bool has_opaque, const char *opaque,
                                Error **errp)
{
    MonFdset *mon_fdset = NULL;
    MonFdsetFd *mon_fdset_fd;
    AddfdInfo *fdinfo;

    qemu_mutex_lock(&mon_fdsets_lock);
    if (has_fdset_id) {
        QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
            /* Break if match found or match impossible due to ordering by ID */
            if (fdset_id <= mon_fdset->id) {
                if (fdset_id < mon_fdset->id) {
                    mon_fdset = NULL;
                }
                break;
            }
        }
    }

    if (mon_fdset == NULL) {
        int64_t fdset_id_prev = -1;
        MonFdset *mon_fdset_cur = QLIST_FIRST(&mon_fdsets);

        if (has_fdset_id) {
            if (fdset_id < 0) {
                error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "fdset-id",
                           "a non-negative value");
                qemu_mutex_unlock(&mon_fdsets_lock);
                return NULL;
            }
            /* Use specified fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id < mon_fdset_cur->id) {
                    break;
                }
            }
        } else {
            /* Use first available fdset ID */
            QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
                mon_fdset_cur = mon_fdset;
                if (fdset_id_prev == mon_fdset_cur->id - 1) {
                    fdset_id_prev = mon_fdset_cur->id;
                    continue;
                }
                break;
            }
        }

        mon_fdset = g_malloc0(sizeof(*mon_fdset));
        if (has_fdset_id) {
            mon_fdset->id = fdset_id;
        } else {
            mon_fdset->id = fdset_id_prev + 1;
        }

        /* The fdset list is ordered by fdset ID */
        if (!mon_fdset_cur) {
            QLIST_INSERT_HEAD(&mon_fdsets, mon_fdset, next);
        } else if (mon_fdset->id < mon_fdset_cur->id) {
            QLIST_INSERT_BEFORE(mon_fdset_cur, mon_fdset, next);
        } else {
            QLIST_INSERT_AFTER(mon_fdset_cur, mon_fdset, next);
        }
    }

    mon_fdset_fd = g_malloc0(sizeof(*mon_fdset_fd));
    mon_fdset_fd->fd = fd;
    mon_fdset_fd->removed = false;
    if (has_opaque) {
        mon_fdset_fd->opaque = g_strdup(opaque);
    }
    QLIST_INSERT_HEAD(&mon_fdset->fds, mon_fdset_fd, next);

    fdinfo = g_malloc0(sizeof(*fdinfo));
    fdinfo->fdset_id = mon_fdset->id;
    fdinfo->fd = mon_fdset_fd->fd;

    qemu_mutex_unlock(&mon_fdsets_lock);
    return fdinfo;
}

int monitor_fdset_get_fd(int64_t fdset_id, int flags)
{
#ifdef _WIN32
    return -ENOENT;
#else
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd;
    int mon_fd_flags;
    int ret;

    qemu_mutex_lock(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd, &mon_fdset->fds, next) {
            mon_fd_flags = fcntl(mon_fdset_fd->fd, F_GETFL);
            if (mon_fd_flags == -1) {
                ret = -errno;
                goto out;
            }

            if ((flags & O_ACCMODE) == (mon_fd_flags & O_ACCMODE)) {
                ret = mon_fdset_fd->fd;
                goto out;
            }
        }
        ret = -EACCES;
        goto out;
    }
    ret = -ENOENT;

out:
    qemu_mutex_unlock(&mon_fdsets_lock);
    return ret;
#endif
}

int monitor_fdset_dup_fd_add(int64_t fdset_id, int dup_fd)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd_dup;

    qemu_mutex_lock(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        if (mon_fdset->id != fdset_id) {
            continue;
        }
        QLIST_FOREACH(mon_fdset_fd_dup, &mon_fdset->dup_fds, next) {
            if (mon_fdset_fd_dup->fd == dup_fd) {
                goto err;
            }
        }
        mon_fdset_fd_dup = g_malloc0(sizeof(*mon_fdset_fd_dup));
        mon_fdset_fd_dup->fd = dup_fd;
        QLIST_INSERT_HEAD(&mon_fdset->dup_fds, mon_fdset_fd_dup, next);
        qemu_mutex_unlock(&mon_fdsets_lock);
        return 0;
    }

err:
    qemu_mutex_unlock(&mon_fdsets_lock);
    return -1;
}

static int64_t monitor_fdset_dup_fd_find_remove(int dup_fd, bool remove)
{
    MonFdset *mon_fdset;
    MonFdsetFd *mon_fdset_fd_dup;

    qemu_mutex_lock(&mon_fdsets_lock);
    QLIST_FOREACH(mon_fdset, &mon_fdsets, next) {
        QLIST_FOREACH(mon_fdset_fd_dup, &mon_fdset->dup_fds, next) {
            if (mon_fdset_fd_dup->fd == dup_fd) {
                if (remove) {
                    QLIST_REMOVE(mon_fdset_fd_dup, next);
                    if (QLIST_EMPTY(&mon_fdset->dup_fds)) {
                        monitor_fdset_cleanup(mon_fdset);
                    }
                    goto err;
                } else {
                    qemu_mutex_unlock(&mon_fdsets_lock);
                    return mon_fdset->id;
                }
            }
        }
    }

err:
    qemu_mutex_unlock(&mon_fdsets_lock);
    return -1;
}

int64_t monitor_fdset_dup_fd_find(int dup_fd)
{
    return monitor_fdset_dup_fd_find_remove(dup_fd, false);
}

void monitor_fdset_dup_fd_remove(int dup_fd)
{
    monitor_fdset_dup_fd_find_remove(dup_fd, true);
}

int monitor_fd_param(Monitor *mon, const char *fdname, Error **errp)
{
    int fd;
    Error *local_err = NULL;

    if (!qemu_isdigit(fdname[0]) && mon) {
        fd = monitor_get_fd(mon, fdname, &local_err);
    } else {
        fd = qemu_parse_fd(fdname);
        if (fd == -1) {
            error_setg(&local_err, "Invalid file descriptor number '%s'",
                       fdname);
        }
    }
    if (local_err) {
        error_propagate(errp, local_err);
        assert(fd == -1);
    } else {
        assert(fd != -1);
    }

    return fd;
}

/* Please update hmp-commands.hx when adding or changing commands */
static HMPCommand hmp_info_cmds[] = {
#include "hmp-commands-info.h"
    { NULL, NULL, },
};

/* hmp_cmds and hmp_info_cmds would be sorted at runtime */
HMPCommand hmp_cmds[] = {
#include "hmp-commands.h"
    { NULL, NULL, },
};

/*
 * Set @pval to the value in the register identified by @name.
 * return 0 if OK, -1 if not found
 */
int get_monitor_def(int64_t *pval, const char *name)
{
    const MonitorDef *md = target_monitor_defs();
    CPUState *cs = mon_get_cpu();
    void *ptr;
    uint64_t tmp = 0;
    int ret;

    if (cs == NULL || md == NULL) {
        return -1;
    }

    for(; md->name != NULL; md++) {
        if (hmp_compare_cmd(name, md->name)) {
            if (md->get_value) {
                *pval = md->get_value(md, md->offset);
            } else {
                CPUArchState *env = mon_get_cpu_env();
                ptr = (uint8_t *)env + md->offset;
                switch(md->type) {
                case MD_I32:
                    *pval = *(int32_t *)ptr;
                    break;
                case MD_TLONG:
                    *pval = *(target_long *)ptr;
                    break;
                default:
                    *pval = 0;
                    break;
                }
            }
            return 0;
        }
    }

    ret = target_get_monitor_def(cs, name, &tmp);
    if (!ret) {
        *pval = (target_long) tmp;
    }

    return ret;
}

static void add_completion_option(ReadLineState *rs, const char *str,
                                  const char *option)
{
    if (!str || !option) {
        return;
    }
    if (!strncmp(option, str, strlen(str))) {
        readline_add_completion(rs, option);
    }
}

void chardev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevBackendInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev_backends(NULL);
    while (list) {
        const char *chr_name = list->value->name;

        if (!strncmp(chr_name, str, len)) {
            readline_add_completion(rs, chr_name);
        }
        list = list->next;
    }
    qapi_free_ChardevBackendInfoList(start);
}

void netdev_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    int i;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; i < NET_CLIENT_DRIVER__MAX; i++) {
        add_completion_option(rs, str, NetClientDriver_str(i));
    }
}

void device_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_DEVICE, false);
    while (elt) {
        const char *name;
        DeviceClass *dc = OBJECT_CLASS_CHECK(DeviceClass, elt->data,
                                             TYPE_DEVICE);
        name = object_class_get_name(OBJECT_CLASS(dc));

        if (dc->user_creatable
            && !strncmp(name, str, len)) {
            readline_add_completion(rs, name);
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

void object_add_completion(ReadLineState *rs, int nb_args, const char *str)
{
    GSList *list, *elt;
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    list = elt = object_class_get_list(TYPE_USER_CREATABLE, false);
    while (elt) {
        const char *name;

        name = object_class_get_name(OBJECT_CLASS(elt->data));
        if (!strncmp(name, str, len) && strcmp(name, TYPE_USER_CREATABLE)) {
            readline_add_completion(rs, name);
        }
        elt = elt->next;
    }
    g_slist_free(list);
}

static void peripheral_device_del_completion(ReadLineState *rs,
                                             const char *str, size_t len)
{
    Object *peripheral = container_get(qdev_get_machine(), "/peripheral");
    GSList *list, *item;

    list = qdev_build_hotpluggable_device_list(peripheral);
    if (!list) {
        return;
    }

    for (item = list; item; item = g_slist_next(item)) {
        DeviceState *dev = item->data;

        if (dev->id && !strncmp(str, dev->id, len)) {
            readline_add_completion(rs, dev->id);
        }
    }

    g_slist_free(list);
}

void chardev_remove_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr = list->value;

        if (!strncmp(chr->label, str, len)) {
            readline_add_completion(rs, chr->label);
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

static void ringbuf_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    ChardevInfoList *list, *start;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_query_chardev(NULL);
    while (list) {
        ChardevInfo *chr_info = list->value;

        if (!strncmp(chr_info->label, str, len)) {
            Chardev *chr = qemu_chr_find(chr_info->label);
            if (chr && CHARDEV_IS_RINGBUF(chr)) {
                readline_add_completion(rs, chr_info->label);
            }
        }
        list = list->next;
    }
    qapi_free_ChardevInfoList(start);
}

void ringbuf_write_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args != 2) {
        return;
    }
    ringbuf_completion(rs, str);
}

void device_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    peripheral_device_del_completion(rs, str, len);
}

void object_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    ObjectPropertyInfoList *list, *start;
    size_t len;

    if (nb_args != 2) {
        return;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);

    start = list = qmp_qom_list("/objects", NULL);
    while (list) {
        ObjectPropertyInfo *info = list->value;

        if (!strncmp(info->type, "child<", 5)
            && !strncmp(info->name, str, len)) {
            readline_add_completion(rs, info->name);
        }
        list = list->next;
    }
    qapi_free_ObjectPropertyInfoList(start);
}

void sendkey_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;
    char *sep;
    size_t len;

    if (nb_args != 2) {
        return;
    }
    sep = strrchr(str, '-');
    if (sep) {
        str = sep + 1;
    }
    len = strlen(str);
    readline_set_completion_index(rs, len);
    for (i = 0; i < Q_KEY_CODE__MAX; i++) {
        if (!strncmp(str, QKeyCode_str(i), len)) {
            readline_add_completion(rs, QKeyCode_str(i));
        }
    }
}

void set_link_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        NetClientState *ncs[MAX_QUEUE_NUM];
        int count, i;
        count = qemu_find_net_clients_except(NULL, ncs,
                                             NET_CLIENT_DRIVER_NONE,
                                             MAX_QUEUE_NUM);
        for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
            const char *name = ncs[i]->name;
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void netdev_del_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int len, count, i;
    NetClientState *ncs[MAX_QUEUE_NUM];

    if (nb_args != 2) {
        return;
    }

    len = strlen(str);
    readline_set_completion_index(rs, len);
    count = qemu_find_net_clients_except(NULL, ncs, NET_CLIENT_DRIVER_NIC,
                                         MAX_QUEUE_NUM);
    for (i = 0; i < MIN(count, MAX_QUEUE_NUM); i++) {
        QemuOpts *opts;
        const char *name = ncs[i]->name;
        if (strncmp(str, name, len)) {
            continue;
        }
        opts = qemu_opts_find(qemu_find_opts_err("netdev", NULL), name);
        if (opts) {
            readline_add_completion(rs, name);
        }
    }
}

void info_trace_events_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventIter iter;
        TraceEvent *ev;
        char *pattern = g_strdup_printf("%s*", str);
        trace_event_iter_init(&iter, pattern);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            readline_add_completion(rs, trace_event_get_name(ev));
        }
        g_free(pattern);
    }
}

void trace_event_completion(ReadLineState *rs, int nb_args, const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        TraceEventIter iter;
        TraceEvent *ev;
        char *pattern = g_strdup_printf("%s*", str);
        trace_event_iter_init(&iter, pattern);
        while ((ev = trace_event_iter_next(&iter)) != NULL) {
            readline_add_completion(rs, trace_event_get_name(ev));
        }
        g_free(pattern);
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void watchdog_action_completion(ReadLineState *rs, int nb_args, const char *str)
{
    int i;

    if (nb_args != 2) {
        return;
    }
    readline_set_completion_index(rs, strlen(str));
    for (i = 0; i < WATCHDOG_ACTION__MAX; i++) {
        add_completion_option(rs, str, WatchdogAction_str(i));
    }
}

void migrate_set_capability_completion(ReadLineState *rs, int nb_args,
                                       const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_CAPABILITY__MAX; i++) {
            const char *name = MigrationCapability_str(i);
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    } else if (nb_args == 3) {
        add_completion_option(rs, str, "on");
        add_completion_option(rs, str, "off");
    }
}

void migrate_set_parameter_completion(ReadLineState *rs, int nb_args,
                                      const char *str)
{
    size_t len;

    len = strlen(str);
    readline_set_completion_index(rs, len);
    if (nb_args == 2) {
        int i;
        for (i = 0; i < MIGRATION_PARAMETER__MAX; i++) {
            const char *name = MigrationParameter_str(i);
            if (!strncmp(str, name, len)) {
                readline_add_completion(rs, name);
            }
        }
    }
}

static void vm_completion(ReadLineState *rs, const char *str)
{
    size_t len;
    BlockDriverState *bs;
    BdrvNextIterator it;

    len = strlen(str);
    readline_set_completion_index(rs, len);

    for (bs = bdrv_first(&it); bs; bs = bdrv_next(&it)) {
        SnapshotInfoList *snapshots, *snapshot;
        AioContext *ctx = bdrv_get_aio_context(bs);
        bool ok = false;

        aio_context_acquire(ctx);
        if (bdrv_can_snapshot(bs)) {
            ok = bdrv_query_snapshot_info_list(bs, &snapshots, NULL) == 0;
        }
        aio_context_release(ctx);
        if (!ok) {
            continue;
        }

        snapshot = snapshots;
        while (snapshot) {
            char *completion = snapshot->value->name;
            if (!strncmp(str, completion, len)) {
                readline_add_completion(rs, completion);
            }
            completion = snapshot->value->id;
            if (!strncmp(str, completion, len)) {
                readline_add_completion(rs, completion);
            }
            snapshot = snapshot->next;
        }
        qapi_free_SnapshotInfoList(snapshots);
    }

}

void delvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}

void loadvm_completion(ReadLineState *rs, int nb_args, const char *str)
{
    if (nb_args == 2) {
        vm_completion(rs, str);
    }
}

static int
compare_mon_cmd(const void *a, const void *b)
{
    return strcmp(((const HMPCommand *)a)->name,
            ((const HMPCommand *)b)->name);
}

static void sortcmdlist(void)
{
    qsort(hmp_cmds, ARRAY_SIZE(hmp_cmds) - 1,
          sizeof(*hmp_cmds),
          compare_mon_cmd);
    qsort(hmp_info_cmds, ARRAY_SIZE(hmp_info_cmds) - 1,
          sizeof(*hmp_info_cmds),
          compare_mon_cmd);
}

void monitor_init_globals(void)
{
    monitor_init_globals_core();
    monitor_init_qmp_commands();
    sortcmdlist();
    qemu_mutex_init(&mon_fdsets_lock);
}
