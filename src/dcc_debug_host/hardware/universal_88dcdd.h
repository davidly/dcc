#ifndef UNIVERSAL_88DCDD_H
#define UNIVERSAL_88DCDD_H

#include "z80.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool host_disk_init(const char *drive_a, const char *drive_b, const char *drive_c, const char *drive_d);
bool host_disk_init_memory_b(const char *drive_a, uint8_t *drive_b, size_t drive_b_size,
							 const char *drive_c, const char *drive_d);
void host_disk_close(void);
disk_controller_t host_disk_controller(void);

#endif
