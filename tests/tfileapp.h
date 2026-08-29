#ifndef TFILEAPP_H
#define TFILEAPP_H

#define TF_BYTES 300

void tf_fill_pattern(unsigned char *buffer, int length);
unsigned long tfsum_checksum(const unsigned char *buffer, int length);
int tf_write_file(const char *name, const unsigned char *buffer, int length);
int tf_read_file(const char *name, unsigned char *buffer, int length);
int tfseek_check_window(const char *name,
                        const unsigned char *expected, int offset, int length);
int tferr_check_paths(void);

#endif
