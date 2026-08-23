#include "universal_88dcdd.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_SECTOR_SIZE 137
#define HOST_SECTORS_PER_TRACK 32
#define HOST_MAX_TRACKS 77
#define HOST_TRACK_SIZE (HOST_SECTORS_PER_TRACK * HOST_SECTOR_SIZE)
#define HOST_DISK_SIZE (HOST_MAX_TRACKS * HOST_TRACK_SIZE)
#define HOST_MAX_DRIVES 4
#define HOST_SECTOR_SHIFT_BITS 1

#define HOST_STATUS_ENWD 1
#define HOST_STATUS_MOVE_HEAD 2
#define HOST_STATUS_HEAD 4
#define HOST_STATUS_SECTOR 8
#define HOST_STATUS_IE 32
#define HOST_STATUS_TRACK_0 64
#define HOST_STATUS_NRDA 128

#define HOST_CONTROL_STEP_IN 1
#define HOST_CONTROL_STEP_OUT 2
#define HOST_CONTROL_HEAD_LOAD 4
#define HOST_CONTROL_HEAD_UNLOAD 8
#define HOST_CONTROL_WE 128

typedef struct {
    FILE *file;
    uint8_t *memory;
    size_t memory_size;
    uint8_t track;
    uint8_t sector;
    uint8_t status;
    uint8_t write_status;
    long disk_pointer;
    uint16_t sector_pointer;
    uint8_t sector_data[HOST_SECTOR_SIZE + 2];
    bool sector_dirty;
    bool have_sector_data;
    bool loaded;
} host_disk_t;

typedef struct {
    host_disk_t disk[HOST_MAX_DRIVES];
    host_disk_t *current;
    uint8_t current_disk;
} host_disk_controller_t;

static host_disk_controller_t g_disk;

static const uint8_t status_default = HOST_STATUS_ENWD | HOST_STATUS_MOVE_HEAD | HOST_STATUS_HEAD |
                                      HOST_STATUS_IE | HOST_STATUS_TRACK_0 | HOST_STATUS_NRDA;

static void set_status(uint8_t bit)
{
    g_disk.current->status &= (uint8_t)~bit;
}

static void clear_status(uint8_t bit)
{
    g_disk.current->status |= bit;
}

static void initialize_disk(host_disk_t *disk)
{
    disk->track = 0;
    disk->sector = 0;
    disk->status = status_default;
    disk->status &= (uint8_t)~HOST_STATUS_MOVE_HEAD;
    disk->status &= (uint8_t)~HOST_STATUS_TRACK_0;
    disk->status &= (uint8_t)~HOST_STATUS_SECTOR;
    disk->write_status = 0;
    disk->disk_pointer = 0;
    disk->sector_pointer = 0;
    disk->sector_dirty = false;
    disk->have_sector_data = false;
    disk->loaded = true;
    memset(disk->sector_data, 0, sizeof(disk->sector_data));
}

static bool open_disk(uint8_t drive, const char *path)
{
    host_disk_t *disk;

    if (drive >= HOST_MAX_DRIVES) {
        return false;
    }

    disk = &g_disk.disk[drive];
    disk->file = fopen(path, "r+b");
    if (!disk->file) {
        return false;
    }

    fseek(disk->file, 0, SEEK_END);
    if (ftell(disk->file) < HOST_DISK_SIZE) {
        fclose(disk->file);
        disk->file = NULL;
        return false;
    }
    fseek(disk->file, 0, SEEK_SET);

    disk->memory = NULL;
    disk->memory_size = 0;
    initialize_disk(disk);
    return true;
}

static bool open_memory_disk(uint8_t drive, uint8_t *memory, size_t memory_size)
{
    host_disk_t *disk;

    if (drive >= HOST_MAX_DRIVES || !memory || memory_size < HOST_DISK_SIZE) {
        return false;
    }

    disk = &g_disk.disk[drive];
    disk->file = NULL;
    disk->memory = memory;
    disk->memory_size = memory_size;
    initialize_disk(disk);
    return true;
}

static void flush_sector(host_disk_t *disk)
{
    if (!disk->loaded || !disk->sector_dirty) {
        return;
    }

    if (disk->memory) {
        size_t offset = (size_t)disk->disk_pointer;
        if (offset + HOST_SECTOR_SIZE <= disk->memory_size) {
            memcpy(disk->memory + offset, disk->sector_data, HOST_SECTOR_SIZE);
        }
    } else {
        fseek(disk->file, disk->disk_pointer, SEEK_SET);
        fwrite(disk->sector_data, 1, HOST_SECTOR_SIZE, disk->file);
        fflush(disk->file);
    }
    disk->sector_dirty = false;
}

static void seek_to_track(void)
{
    host_disk_t *disk = g_disk.current;

    if (!disk->loaded) {
        return;
    }

    flush_sector(disk);
    disk->disk_pointer = (long)disk->track * HOST_TRACK_SIZE;
    disk->sector = 0;
    disk->sector_pointer = 0;
    disk->have_sector_data = false;
}

static void host_disk_select(uint8_t drive)
{
    uint8_t select = drive & 0x0f;

    if (select >= HOST_MAX_DRIVES) {
        select = 0;
    }

    g_disk.current_disk = select;
    g_disk.current = &g_disk.disk[select];
}

static uint8_t host_disk_status(void)
{
    return g_disk.current->status;
}

static void host_disk_function(uint8_t control)
{
    host_disk_t *disk = g_disk.current;

    if (!disk->loaded) {
        return;
    }

    if (control & HOST_CONTROL_STEP_IN) {
        if (disk->track < HOST_MAX_TRACKS - 1) {
            disk->track++;
        }
        if (disk->track != 0) {
            clear_status(HOST_STATUS_TRACK_0);
        }
        seek_to_track();
    }

    if (control & HOST_CONTROL_STEP_OUT) {
        if (disk->track > 0) {
            disk->track--;
        }
        if (disk->track == 0) {
            set_status(HOST_STATUS_TRACK_0);
        }
        seek_to_track();
    }

    if (control & HOST_CONTROL_HEAD_LOAD) {
        set_status(HOST_STATUS_HEAD);
        set_status(HOST_STATUS_NRDA);
    }

    if (control & HOST_CONTROL_HEAD_UNLOAD) {
        clear_status(HOST_STATUS_HEAD);
    }

    if (control & HOST_CONTROL_WE) {
        set_status(HOST_STATUS_ENWD);
        disk->write_status = 0;
    }
}

static uint8_t host_disk_sector(void)
{
    host_disk_t *disk = g_disk.current;
    uint8_t ret_val;

    if (!disk->loaded) {
        return 0xc0;
    }

    if (disk->sector == HOST_SECTORS_PER_TRACK) {
        disk->sector = 0;
    }

    flush_sector(disk);
    disk->disk_pointer = ((long)disk->track * HOST_TRACK_SIZE) + ((long)disk->sector * HOST_SECTOR_SIZE);
    disk->sector_pointer = 0;
    disk->have_sector_data = false;

    ret_val = 0xc0;
    ret_val |= (uint8_t)(disk->sector << HOST_SECTOR_SHIFT_BITS);
    ret_val |= (disk->sector_pointer == 0) ? 0 : 1;
    disk->sector++;
    return ret_val;
}

static void host_disk_write(uint8_t data)
{
    host_disk_t *disk = g_disk.current;

    if (!disk->loaded) {
        return;
    }

    if (disk->sector_pointer >= HOST_SECTOR_SIZE + 2) {
        disk->sector_pointer = HOST_SECTOR_SIZE + 1;
    }

    disk->sector_data[disk->sector_pointer++] = data;
    disk->sector_dirty = true;
    disk->have_sector_data = true;

    if (disk->write_status == HOST_SECTOR_SIZE) {
        flush_sector(disk);
        disk->write_status = 0;
        clear_status(HOST_STATUS_ENWD);
    } else {
        disk->write_status++;
    }
}

static uint8_t host_disk_read(void)
{
    host_disk_t *disk = g_disk.current;

    if (!disk->loaded) {
        return 0x00;
    }

    if (!disk->have_sector_data) {
        memset(disk->sector_data, 0, sizeof(disk->sector_data));
        if (disk->memory) {
            size_t offset = (size_t)disk->disk_pointer;
            if (offset + HOST_SECTOR_SIZE <= disk->memory_size) {
                memcpy(disk->sector_data, disk->memory + offset, HOST_SECTOR_SIZE);
            }
        } else {
            fseek(disk->file, disk->disk_pointer, SEEK_SET);
            if (fread(disk->sector_data, 1, HOST_SECTOR_SIZE, disk->file) != HOST_SECTOR_SIZE) {
                clearerr(disk->file);
            }
        }
        disk->sector_pointer = 0;
        disk->have_sector_data = true;
    }

    if (disk->sector_pointer >= sizeof(disk->sector_data)) {
        return 0x00;
    }

    return disk->sector_data[disk->sector_pointer++];
}

bool host_disk_init(const char *drive_a, const char *drive_b, const char *drive_c, const char *drive_d)
{
    memset(&g_disk, 0, sizeof(g_disk));
    g_disk.current = &g_disk.disk[0];

    if (!open_disk(0, drive_a)) {
        return false;
    }
    if (!open_disk(1, drive_b)) {
        host_disk_close();
        return false;
    }
    if (!open_disk(2, drive_c)) {
        host_disk_close();
        return false;
    }
    if (!open_disk(3, drive_d)) {
        host_disk_close();
        return false;
    }

    return true;
}

bool host_disk_init_memory_b(const char *drive_a, uint8_t *drive_b, size_t drive_b_size,
                             const char *drive_c, const char *drive_d)
{
    memset(&g_disk, 0, sizeof(g_disk));
    g_disk.current = &g_disk.disk[0];

    if (!open_disk(0, drive_a)) {
        return false;
    }
    if (!open_memory_disk(1, drive_b, drive_b_size)) {
        host_disk_close();
        return false;
    }
    if (!open_disk(2, drive_c)) {
        host_disk_close();
        return false;
    }
    if (!open_disk(3, drive_d)) {
        host_disk_close();
        return false;
    }

    return true;
}

void host_disk_close(void)
{
    int i;

    for (i = 0; i < HOST_MAX_DRIVES; i++) {
        if (g_disk.disk[i].loaded) {
            flush_sector(&g_disk.disk[i]);
        }
        if (g_disk.disk[i].file) {
            fclose(g_disk.disk[i].file);
            g_disk.disk[i].file = NULL;
        }
        g_disk.disk[i].memory = NULL;
        g_disk.disk[i].memory_size = 0;
        g_disk.disk[i].loaded = false;
    }
}

disk_controller_t host_disk_controller(void)
{
    disk_controller_t controller;

    controller.disk_select = host_disk_select;
    controller.disk_status = host_disk_status;
    controller.disk_function = host_disk_function;
    controller.sector = host_disk_sector;
    controller.write = host_disk_write;
    controller.read = host_disk_read;
    return controller;
}
