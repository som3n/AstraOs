#ifndef FAT16_H
#define FAT16_H

#include <stdint.h>

typedef struct
{
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint16_t sectors_per_fat;
    uint32_t total_sectors_32;
} fat16_bpb_t;

typedef struct
{
    char name[8];
    char ext[3];
    uint8_t attr;
    uint8_t reserved;
    uint8_t creation_time_tenths;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed)) fat16_dir_entry_t;

/* init */
int fat16_init();
fat16_bpb_t fat16_get_bpb();

/* navigation */
void fat16_ls();
int fat16_ls_path(const char *path);

int fat16_cd_path(const char *path);
void fat16_pwd();
const char *fat16_get_path();

/* file reading */
int fat16_cat(const char *filename);

/* file creation */
int fat16_touch(const char *filename);
int fat16_mkdir(const char *dirname);
int fat16_mkdir_p(const char *path);

/* file deletion */
int fat16_rm(const char *filename);
int fat16_rmdir(const char *dirname);
int fat16_rm_rf(const char *path);

/* file writing */
int fat16_write_file(const char *path, const uint8_t *data, uint32_t size);
int fat16_append_file(const char *path, const uint8_t *data, uint32_t size);

/* file operations */
int fat16_cp(const char *src, const char *dst);
int fat16_mv(const char *src, const char *dst);

#endif
