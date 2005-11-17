/*
 * dm.c - backend library for partition table scanning on dm devices
 * 
 * Copyright 2005 Peter M. Jones
 * Copyright 2005 Red Hat, Inc.
 * 
 * vim:ts=8:sw=4:sts=4:et
 *
 */

#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <libdevmapper.h>
#include <parted/parted.h>

#include "dm.h"

/* XXX this should be nash-wide, not just in dm.c.  FIXME when we move
   to libnash.a */
int nashDefaultLogger(nash_log_level level, char *format, ...)
{
    int ret;
    va_list ap;

    va_start(ap, format);
    ret = vprintf(format, ap);
    va_end(ap);

    fflush(stdout);
    return ret;
}

int nashLogger(const nash_log_level, const char *format, ...)
    __attribute__ ((weak, alias("nashDefaultLogger")));

int
nashDmCreate(char *name, long long start, long long length,
        char *type, char *params)
{
    struct dm_task *task;
    int rc;

    task = dm_task_create(DM_DEVICE_CREATE);
    if (!task)
        return 0;

    dm_task_set_name(task, name);

    dm_task_add_target(task, start, length, type, params);

    rc = dm_task_run(task);
    dm_task_destroy(task);

    if (rc < 0)
        return 0;

    return 1;
}

int
nashDmRemove(char *name)
{
    struct dm_task *task;
    int rc;

    task = dm_task_create(DM_DEVICE_REMOVE);
    if (!task)
        return 0;

    dm_task_set_name(task, name);

    rc = dm_task_run(task);
    dm_task_destroy(task);

    if (rc < 0)
        return 0;

    return 1;
}

static int nashPartedError = 0;

static PedExceptionOption
nashPartedExceptionHandler(PedException *ex)
{
    nash_log_level level;

    switch (ex->type) {
        case PED_EXCEPTION_INFORMATION:
            nashPartedError = 0;
            level = NOTICE;
            break;
        case PED_EXCEPTION_WARNING:
            nashPartedError = 0;
            level = WARNING;
            break;
        default:
            level = ERROR;
            nashPartedError = 1;
            break;
    }
    nashLogger(level, ex->message);
    switch (ex->options) {
        case PED_EXCEPTION_OK:
        case PED_EXCEPTION_CANCEL:
        case PED_EXCEPTION_IGNORE:
            return ex->options;
        default:
            break;
    }
    return PED_EXCEPTION_UNHANDLED;
}

int
nashDmCreatePartitions(char *path)
{
    PedDevice *dev;
    PedDisk *disk;
    PedPartition *part = NULL;
    char *namestart;
    int nparts = 0;
    struct stat sb;

    if (stat(path, &sb) < 0 || !S_ISBLK(sb.st_mode))
        return nparts;

    namestart = strrchr(path, '/');
    if (!namestart || !*(namestart++))
        goto out;
    if (!*namestart)
        goto out;

    ped_exception_set_handler(nashPartedExceptionHandler);

    dev = ped_device_get(path);
    if (!dev || nashPartedError) 
        goto out; 

    if (!ped_device_open(dev))
        goto out;
    
    disk = ped_disk_new(dev);
    if (!disk || nashPartedError)
        goto out;

    part = ped_disk_next_partition(disk, NULL);
    while (part) {
        if (ped_partition_is_active(part))  {
            int i;
            char *name = NULL;
            char *table = NULL;

            i = asprintf(&name, "%s%d", namestart, part->num);
            if (i < 0)
                continue;

            i = asprintf(&table, "%d:%d %Ld", major(sb.st_rdev),
                        minor(sb.st_rdev), part->geom.start);
            if (i < 0) {
                free(name);
                continue;
            }

            nparts += nashDmCreate(name, 0, part->geom.length,
                    "linear", table);

            free(table);
            free(name);
        }
        part = ped_disk_next_partition(disk, part);
    }
out:
    if (disk) {
        ped_disk_destroy(disk);
        ped_device_close(dev);
    }

    if (dev)
        ped_device_destroy(dev);

    nashPartedError = 0;
    return nparts;
}

#if 0 /* notyet */
extern int
nashDmRemovePartitions(char *name)
{

}
#endif
