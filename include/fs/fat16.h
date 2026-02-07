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

void fat16_list_root();

int fat16_init();
fat16_bpb_t fat16_get_bpb();
int fat16_cat_root(const char *filename);
void fat16_ls();
int fat16_cd(const char *dirname);
void fat16_pwd();
int fat16_cat(const char *filename);
char *strcat(char *dest, const char *src);

#endif
