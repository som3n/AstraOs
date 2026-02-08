#include "fs/fat16.h"
#include "drivers/ata.h"
#include "vga.h"
#include "kernel/print.h"
#include <string.h>

static fat16_bpb_t bpb;

static uint16_t current_dir_cluster = 0; // 0 = root
static char current_path[128] = "/";

static uint16_t fat16_get_parent_cluster(uint16_t dir_cluster);


/* ------------------- FAT16 Helpers ------------------- */

static uint32_t fat16_root_start_sector()
{
    return bpb.reserved_sectors + (bpb.num_fats * bpb.sectors_per_fat);
}

static uint32_t fat16_root_dir_sectors()
{
    return (bpb.root_entries * 32 + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
}

static uint32_t fat16_first_data_sector()
{
    return fat16_root_start_sector() + fat16_root_dir_sectors();
}

static uint32_t fat16_cluster_to_sector(uint16_t cluster)
{
    return fat16_first_data_sector() + (cluster - 2) * bpb.sectors_per_cluster;
}

static uint16_t fat16_get_fat_entry(uint16_t cluster)
{

    uint32_t fat_start = bpb.reserved_sectors;
    uint32_t fat_offset = cluster * 2; // FAT16 = 2 bytes per entry

    uint32_t sector_num = fat_start + (fat_offset / 512);
    uint32_t offset = fat_offset % 512;

    uint8_t sector[512];
    ata_read_sector(sector_num, sector);

    return *(uint16_t *)&sector[offset];
}

static void fat16_format_filename(const char *input, char *out11)
{

    for (int i = 0; i < 11; i++)
        out11[i] = ' ';

    int i = 0;
    int j = 0;

    // name part
    while (input[i] != '\0' && input[i] != '.' && j < 8)
    {
        char c = input[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        out11[j++] = c;
        i++;
    }

    // extension part
    if (input[i] == '.')
    {
        i++;
        j = 8;
        int k = 0;

        while (input[i] != '\0' && k < 3)
        {
            char c = input[i];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            out11[j++] = c;
            i++;
            k++;
        }
    }
}

static void fat16_entry_to_name(fat16_dir_entry_t *entry, char *out)
{

    int pos = 0;

    for (int j = 0; j < 8; j++)
    {
        if (entry->name[j] == ' ')
            break;
        out[pos++] = entry->name[j];
    }

    if (entry->ext[0] != ' ')
    {
        out[pos++] = '.';
        for (int j = 0; j < 3; j++)
        {
            if (entry->ext[j] == ' ')
                break;
            out[pos++] = entry->ext[j];
        }
    }

    out[pos] = '\0';
}

/* Find file/dir entry inside root or cluster directory */
static int fat16_find_entry(uint16_t dir_cluster, const char *name, fat16_dir_entry_t *out)
{

    char fatname[11];
    fat16_format_filename(name, fatname);

    uint8_t sector[512];

    // ROOT directory special case
    if (dir_cluster == 0)
    {

        uint32_t root_start = fat16_root_start_sector();
        uint32_t root_sectors = fat16_root_dir_sectors();

        for (uint32_t s = 0; s < root_sectors; s++)
        {

            ata_read_sector(root_start + s, sector);

            for (int i = 0; i < 512; i += 32)
            {

                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00)
                    return 0;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                int match = 1;

                for (int j = 0; j < 8; j++)
                {
                    if (entry->name[j] != fatname[j])
                    {
                        match = 0;
                        break;
                    }
                }

                if (match)
                {
                    for (int j = 0; j < 3; j++)
                    {
                        if (entry->ext[j] != fatname[8 + j])
                        {
                            match = 0;
                            break;
                        }
                    }
                }

                if (match)
                {
                    *out = *entry;
                    return 1;
                }
            }
        }

        return 0;
    }

    // SUBDIRECTORY case (cluster chain)
    uint16_t cluster = dir_cluster;

    while (cluster < 0xFFF8)
    {

        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {

            ata_read_sector(start_sector + s, sector);

            for (int i = 0; i < 512; i += 32)
            {

                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00)
                    return 0;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                int match = 1;

                for (int j = 0; j < 8; j++)
                {
                    if (entry->name[j] != fatname[j])
                    {
                        match = 0;
                        break;
                    }
                }

                if (match)
                {
                    for (int j = 0; j < 3; j++)
                    {
                        if (entry->ext[j] != fatname[8 + j])
                        {
                            match = 0;
                            break;
                        }
                    }
                }

                if (match)
                {
                    *out = *entry;
                    return 1;
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 0;
}

/* ------------------- FAT16 Public API ------------------- */

int fat16_init()
{

    uint8_t sector[512];
    ata_read_sector(0, sector);

    bpb.bytes_per_sector = *(uint16_t *)&sector[11];
    bpb.sectors_per_cluster = sector[13];
    bpb.reserved_sectors = *(uint16_t *)&sector[14];
    bpb.num_fats = sector[16];
    bpb.root_entries = *(uint16_t *)&sector[17];
    bpb.total_sectors_16 = *(uint16_t *)&sector[19];
    bpb.sectors_per_fat = *(uint16_t *)&sector[22];
    bpb.total_sectors_32 = *(uint32_t *)&sector[32];

    if (sector[510] != 0x55 || sector[511] != 0xAA)
    {
        return 0;
    }

    if (bpb.bytes_per_sector != 512)
    {
        return 0;
    }

    return 1;
}

fat16_bpb_t fat16_get_bpb()
{
    return bpb;
}

void fat16_pwd()
{
    print("\n");
    print(current_path);
    print("\n");
}

void fat16_ls()
{

    uint8_t sector[512];

    // ROOT directory listing
    if (current_dir_cluster == 0)
    {

        uint32_t root_start = fat16_root_start_sector();
        uint32_t root_sectors = fat16_root_dir_sectors();

        for (uint32_t s = 0; s < root_sectors; s++)
        {

            ata_read_sector(root_start + s, sector);

            for (int i = 0; i < 512; i += 32)
            {

                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00)
                    return;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                char filename[13];
                fat16_entry_to_name(entry, filename);

                if (entry->attr & 0x10)
                {
                    print("DIR   ");
                    print(filename);
                    print("\n");
                }
                else
                {
                    print("FILE  ");
                    print(filename);

                    int name_len = strlen(filename);
                    if (name_len < 12)
                    {
                        for (int k = 0; k < (12 - name_len); k++)
                        {
                            print(" ");
                        }
                    }
                    else
                    {
                        print(" ");
                    }

                    print(" ");
                    print_uint(entry->file_size);
                    print(" bytes\n");
                }
            }
        }

        return;
    }

    // SUBDIRECTORY listing
    uint16_t cluster = current_dir_cluster;

    while (cluster < 0xFFF8)
    {

        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {

            ata_read_sector(start_sector + s, sector);

            for (int i = 0; i < 512; i += 32)
            {

                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00)
                    return;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                char filename[13];
                fat16_entry_to_name(entry, filename);

                if (entry->attr & 0x10)
                {
                    print("DIR   ");
                    print(filename);
                    print("\n");
                }
                else
                {
                    print("FILE  ");
                    print(filename);

                    int name_len = strlen(filename);
                    if (name_len < 12)
                    {
                        for (int k = 0; k < (12 - name_len); k++)
                        {
                            print(" ");
                        }
                    }
                    else
                    {
                        print(" ");
                    }

                    print(" ");
                    print_uint(entry->file_size);
                    print(" bytes\n");
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }
}

int fat16_cd(const char *dirname)
{

    // go root
    if (strcmp(dirname, "/") == 0)
    {
        current_dir_cluster = 0;
        strcpy(current_path, "/");
        return 1;
    }

    if (strcmp(dirname, "..") == 0)
    {

        if (current_dir_cluster == 0)
        {
            strcpy(current_path, "/");
            return 1;
        }

        uint16_t parent = fat16_get_parent_cluster(current_dir_cluster);
        current_dir_cluster = parent;

        // remove last part of path
        int len = strlen(current_path);

        if (len > 1)
        {
            for (int i = len - 1; i >= 0; i--)
            {
                if (current_path[i] == '/')
                {
                    if (i == 0)
                    {
                        current_path[1] = '\0';
                    }
                    else
                    {
                        current_path[i] = '\0';
                    }
                    break;
                }
            }
        }

        return 1;
    }

    fat16_dir_entry_t entry;

    if (!fat16_find_entry(current_dir_cluster, dirname, &entry))
    {
        return 0;
    }

    if (!(entry.attr & 0x10))
    {
        return 0;
    }

    current_dir_cluster = entry.first_cluster_low;

    // update path string
    if (strcmp(current_path, "/") == 0)
    {
        strcat(current_path, dirname);
    }
    else
    {
        strcat(current_path, "/");
        strcat(current_path, dirname);
    }

    return 1;
}

int fat16_cat(const char *filename)
{

    fat16_dir_entry_t entry;

    if (!fat16_find_entry(current_dir_cluster, filename, &entry))
    {
        return 0;
    }

    if (entry.attr & 0x10)
    {
        print("\nCannot cat a directory.\n");
        return 1;
    }

    uint32_t remaining = entry.file_size;
    uint16_t cluster = entry.first_cluster_low;

    uint8_t buf[512];

    print("\n");

    while (cluster < 0xFFF8)
    {

        uint32_t sector_num = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {

            ata_read_sector(sector_num + s, buf);

            for (int i = 0; i < 512; i++)
            {

                if (remaining == 0)
                {
                    print("\n");
                    return 1;
                }

                char c = (char)buf[i];

                if (c == '\n' || c == '\r' || (c >= 32 && c <= 126))
                {
                    print_char(c);
                }
                else
                {
                    print_char('.');
                }

                remaining--;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    print("\n");
    return 1;
}

char *strcat(char *dest, const char *src)
{
    int i = 0;
    int j = 0;

    while (dest[i] != '\0')
    {
        i++;
    }

    while (src[j] != '\0')
    {
        dest[i++] = src[j++];
    }

    dest[i] = '\0';
    return dest;
}

const char *fat16_get_path()
{
    return current_path;
}

static uint16_t fat16_get_parent_cluster(uint16_t dir_cluster)
{

    if (dir_cluster == 0)
    {
        return 0;
    }

    uint8_t sector[512];
    uint16_t cluster = dir_cluster;

    while (cluster < 0xFFF8)
    {

        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {

            ata_read_sector(start_sector + s, sector);

            for (int i = 0; i < 512; i += 32)
            {

                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00)
                    return 0;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                // ".." entry in FAT16
                if (entry->name[0] == '.' && entry->name[1] == '.')
                {
                    return entry->first_cluster_low;
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 0;
}

int fat16_touch(const char *filename) {

    if (filename[0] == '\0') {
        return 0;
    }

    char fatname[11];
    fat16_format_filename(filename, fatname);

    uint8_t sector[512];

    // ROOT directory special case only (for now)
    if (current_dir_cluster != 0) {
        print("\nOnly root touch supported for now.\n");
        return 0;
    }

    uint32_t root_start =
        bpb.reserved_sectors + (bpb.num_fats * bpb.sectors_per_fat);

    uint32_t root_dir_sectors =
        (bpb.root_entries * 32 + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;

    for (uint32_t s = 0; s < root_dir_sectors; s++) {

        ata_read_sector(root_start + s, sector);

        for (int i = 0; i < 512; i += 32) {

            fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

            // free entry (0x00 or deleted 0xE5)
            if (entry->name[0] == 0x00 || (uint8_t)entry->name[0] == 0xE5) {

                // write filename
                for (int j = 0; j < 8; j++) entry->name[j] = fatname[j];
                for (int j = 0; j < 3; j++) entry->ext[j] = fatname[8 + j];

                entry->attr = 0x20; // archive / normal file
                entry->reserved = 0;
                entry->create_time_tenths = 0;
                entry->create_time = 0;
                entry->create_date = 0;
                entry->last_access_date = 0;
                entry->first_cluster_high = 0;
                entry->write_time = 0;
                entry->write_date = 0;
                entry->first_cluster_low = 0; // empty file
                entry->file_size = 0;

                ata_write_sector(root_start + s, sector);
                return 1;
            }
        }
    }

    return 0;
}