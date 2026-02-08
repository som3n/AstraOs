#include "fs/fat16.h"
#include "drivers/ata.h"
#include "vga.h"
#include "kernel/print.h"
#include "string.h"

static fat16_bpb_t bpb;

static uint16_t current_dir_cluster = 0; // 0 = root
static char current_path[128] = "/";

/* ----------- Forward Declarations (IMPORTANT) ----------- */
static uint16_t fat16_get_parent_cluster(uint16_t dir_cluster);
static int fat16_find_free_dir_entry(uint16_t dir_cluster, uint32_t *out_sector, uint32_t *out_offset);

static void fat16_normalize_path(const char *base, const char *input, char *out);
static int fat16_resolve_absolute(const char *path, uint16_t *out_cluster);

static uint32_t fat16_root_start_sector();
static uint32_t fat16_root_dir_sectors();
static uint32_t fat16_first_data_sector();

static uint32_t fat16_cluster_to_sector(uint16_t cluster);
static uint16_t fat16_get_fat_entry(uint16_t cluster);
static void fat16_set_fat_entry(uint16_t cluster, uint16_t value);

static void fat16_format_filename(const char *input, char *out11);
static void fat16_entry_to_name(fat16_dir_entry_t *entry, char *out);

static int fat16_find_entry(uint16_t dir_cluster, const char *name, fat16_dir_entry_t *out);
static int fat16_find_entry_location(uint16_t dir_cluster, const char *name,
                                     uint32_t *out_sector, uint32_t *out_offset,
                                     fat16_dir_entry_t *out_entry);

static uint16_t fat16_alloc_cluster();
static void fat16_clear_cluster(uint16_t cluster);

static void fat16_free_cluster_chain(uint16_t start_cluster);
static int fat16_is_dir_empty(uint16_t dir_cluster);

static int fat16_delete_dir_recursive(uint16_t dir_cluster);

/* ------------------- FAT16 Helpers ------------------- */

static int fat16_delete_dir_recursive(uint16_t dir_cluster)
{
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
                    goto done;

                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;

                if (entry->attr == 0x0F)
                    continue;

                // skip "." and ".."
                if (entry->name[0] == '.' &&
                    (entry->name[1] == ' ' || entry->name[1] == '.'))
                {
                    continue;
                }

                // directory
                if (entry->attr & 0x10)
                {
                    uint16_t sub_cluster = entry->first_cluster_low;

                    if (sub_cluster >= 2)
                        fat16_delete_dir_recursive(sub_cluster);

                    entry->name[0] = 0xE5;
                    ata_write_sector(start_sector + s, sector);
                }
                else
                {
                    if (entry->first_cluster_low != 0)
                        fat16_free_cluster_chain(entry->first_cluster_low);

                    entry->name[0] = 0xE5;
                    ata_write_sector(start_sector + s, sector);
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

done:
    fat16_free_cluster_chain(dir_cluster);
    return 1;
}

static void fat16_normalize_path(const char *base, const char *input, char *out)
{
    char temp[128];
    int ti = 0;

    if (input[0] == '/')
    {
        temp[ti++] = '/';
    }
    else
    {
        for (int i = 0; base[i] != '\0'; i++)
            temp[ti++] = base[i];

        if (ti > 1 && temp[ti - 1] != '/')
            temp[ti++] = '/';
    }

    for (int i = 0; input[i] != '\0'; i++)
        temp[ti++] = input[i];

    temp[ti] = '\0';

    char stack[16][32];
    int top = 0;

    int i = 0;
    while (temp[i] != '\0')
    {
        while (temp[i] == '/')
            i++;

        if (temp[i] == '\0')
            break;

        char part[32];
        int pi = 0;

        while (temp[i] != '\0' && temp[i] != '/')
        {
            if (pi < 31)
                part[pi++] = temp[i];
            i++;
        }

        part[pi] = '\0';

        if (strcmp(part, ".") == 0)
            continue;

        if (strcmp(part, "..") == 0)
        {
            if (top > 0)
                top--;
            continue;
        }

        strcpy(stack[top], part);
        top++;
    }

    int oi = 0;
    out[oi++] = '/';

    for (int j = 0; j < top; j++)
    {
        int k = 0;
        while (stack[j][k])
            out[oi++] = stack[j][k++];

        if (j != top - 1)
            out[oi++] = '/';
    }

    out[oi] = '\0';
}

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
    uint32_t fat_offset = cluster * 2;

    uint32_t sector_num = fat_start + (fat_offset / 512);
    uint32_t offset = fat_offset % 512;

    uint8_t sector[512];
    ata_read_sector(sector_num, sector);

    return *(uint16_t *)&sector[offset];
}

static void fat16_set_fat_entry(uint16_t cluster, uint16_t value)
{
    uint32_t fat_start = bpb.reserved_sectors;
    uint32_t fat_offset = cluster * 2;

    uint32_t sector_num = fat_start + (fat_offset / 512);
    uint32_t offset = fat_offset % 512;

    uint8_t sector[512];

    ata_read_sector(sector_num, sector);
    *(uint16_t *)&sector[offset] = value;
    ata_write_sector(sector_num, sector);

    uint32_t fat2_start = fat_start + bpb.sectors_per_fat;
    ata_read_sector(fat2_start + (fat_offset / 512), sector);
    *(uint16_t *)&sector[offset] = value;
    ata_write_sector(fat2_start + (fat_offset / 512), sector);
}

static void fat16_format_filename(const char *input, char *out11)
{
    for (int i = 0; i < 11; i++)
        out11[i] = ' ';

    int i = 0;
    int j = 0;

    while (input[i] != '\0' && input[i] != '.' && j < 8)
    {
        char c = input[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        out11[j++] = c;
        i++;
    }

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

static int fat16_find_entry(uint16_t dir_cluster, const char *name, fat16_dir_entry_t *out)
{
    char fatname[11];
    fat16_format_filename(name, fatname);

    uint8_t sector[512];

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

static uint16_t fat16_alloc_cluster()
{
    uint32_t total_sectors = (bpb.total_sectors_16 != 0) ? bpb.total_sectors_16 : bpb.total_sectors_32;

    uint32_t data_sectors =
        total_sectors -
        (bpb.reserved_sectors + (bpb.num_fats * bpb.sectors_per_fat) + fat16_root_dir_sectors());

    uint32_t total_clusters = data_sectors / bpb.sectors_per_cluster;

    for (uint16_t cluster = 2; cluster < total_clusters + 2; cluster++)
    {
        if (fat16_get_fat_entry(cluster) == 0x0000)
        {
            fat16_set_fat_entry(cluster, 0xFFFF);
            return cluster;
        }
    }

    return 0;
}

static void fat16_clear_cluster(uint16_t cluster)
{
    uint8_t zero[512];
    for (int i = 0; i < 512; i++)
        zero[i] = 0;

    uint32_t start_sector = fat16_cluster_to_sector(cluster);

    for (int s = 0; s < bpb.sectors_per_cluster; s++)
        ata_write_sector(start_sector + s, zero);
}

static int fat16_find_free_dir_entry(uint16_t dir_cluster, uint32_t *out_sector, uint32_t *out_offset)
{
    uint8_t sector[512];

    if (dir_cluster == 0)
    {
        uint32_t root_start = fat16_root_start_sector();
        uint32_t root_sectors = fat16_root_dir_sectors();

        for (uint32_t s = 0; s < root_sectors; s++)
        {
            ata_read_sector(root_start + s, sector);

            for (uint32_t i = 0; i < 512; i += 32)
            {
                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00 || (uint8_t)entry->name[0] == 0xE5)
                {
                    *out_sector = root_start + s;
                    *out_offset = i;
                    return 1;
                }
            }
        }

        return 0;
    }

    uint16_t cluster = dir_cluster;

    while (cluster < 0xFFF8)
    {
        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            ata_read_sector(start_sector + s, sector);

            for (uint32_t i = 0; i < 512; i += 32)
            {
                fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[i];

                if (entry->name[0] == 0x00 || (uint8_t)entry->name[0] == 0xE5)
                {
                    *out_sector = start_sector + s;
                    *out_offset = i;
                    return 1;
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 0;
}

static void fat16_free_cluster_chain(uint16_t start_cluster)
{
    uint16_t cluster = start_cluster;

    while (cluster >= 2 && cluster < 0xFFF8)
    {
        uint16_t next = fat16_get_fat_entry(cluster);
        fat16_set_fat_entry(cluster, 0x0000);
        cluster = next;
    }
}

static int fat16_find_entry_location(uint16_t dir_cluster, const char *name,
                                     uint32_t *out_sector, uint32_t *out_offset,
                                     fat16_dir_entry_t *out_entry)
{
    char fatname[11];
    fat16_format_filename(name, fatname);

    uint8_t sector[512];

    if (dir_cluster == 0)
    {
        uint32_t root_start = fat16_root_start_sector();
        uint32_t root_sectors = fat16_root_dir_sectors();

        for (uint32_t s = 0; s < root_sectors; s++)
        {
            ata_read_sector(root_start + s, sector);

            for (uint32_t i = 0; i < 512; i += 32)
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
                    if (out_sector) *out_sector = root_start + s;
                    if (out_offset) *out_offset = i;
                    if (out_entry) *out_entry = *entry;
                    return 1;
                }
            }
        }

        return 0;
    }

    uint16_t cluster = dir_cluster;

    while (cluster < 0xFFF8)
    {
        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            ata_read_sector(start_sector + s, sector);

            for (uint32_t i = 0; i < 512; i += 32)
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
                    if (out_sector) *out_sector = start_sector + s;
                    if (out_offset) *out_offset = i;
                    if (out_entry) *out_entry = *entry;
                    return 1;
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 0;
}

static int fat16_is_dir_empty(uint16_t dir_cluster)
{
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
                    return 1;

                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;

                if (entry->attr == 0x0F)
                    continue;

                if (entry->name[0] == '.' &&
                    (entry->name[1] == ' ' || entry->name[1] == '.'))
                {
                    continue;
                }

                return 0;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 1;
}

static int fat16_resolve_absolute(const char *path, uint16_t *out_cluster)
{
    if (!path || path[0] != '/')
        return 0;

    uint16_t cluster = 0;
    path++;

    char part[32];
    int pi = 0;

    for (int i = 0;; i++)
    {
        char c = path[i];

        if (c == '/' || c == '\0')
        {
            part[pi] = '\0';

            if (pi > 0)
            {
                fat16_dir_entry_t entry;

                if (!fat16_find_entry(cluster, part, &entry))
                    return 0;

                if (!(entry.attr & 0x10))
                    return 0;

                cluster = entry.first_cluster_low;
            }

            pi = 0;

            if (c == '\0')
                break;
        }
        else
        {
            if (pi < 31)
                part[pi++] = c;
        }
    }

    *out_cluster = cluster;
    return 1;
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
        return 0;

    if (bpb.bytes_per_sector != 512)
        return 0;

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
                            print(" ");
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
                            print(" ");
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

int fat16_cd_path(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    uint16_t new_cluster;
    if (!fat16_resolve_absolute(abs, &new_cluster))
        return 0;

    current_dir_cluster = new_cluster;
    strcpy(current_path, abs);

    return 1;
}

int fat16_cat(const char *filename)
{
    fat16_dir_entry_t entry;

    if (!fat16_find_entry(current_dir_cluster, filename, &entry))
        return 0;

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
                    print_char(c);
                else
                    print_char('.');

                remaining--;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    print("\n");
    return 1;
}

const char *fat16_get_path()
{
    return current_path;
}

static uint16_t fat16_get_parent_cluster(uint16_t dir_cluster)
{
    if (dir_cluster == 0)
        return 0;

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

                if (entry->name[0] == '.' && entry->name[1] == '.')
                    return entry->first_cluster_low;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 0;
}

/* ------------------- File Commands ------------------- */

int fat16_touch(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return 0;

    char fatname[11];
    fat16_format_filename(filename, fatname);

    fat16_dir_entry_t existing;
    if (fat16_find_entry(current_dir_cluster, filename, &existing))
        return 0;

    uint32_t free_sector;
    uint32_t free_offset;

    if (!fat16_find_free_dir_entry(current_dir_cluster, &free_sector, &free_offset))
        return 0;

    uint8_t sector[512];
    ata_read_sector(free_sector, sector);

    fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[free_offset];

    for (int i = 0; i < 32; i++)
        ((uint8_t *)entry)[i] = 0;

    for (int j = 0; j < 8; j++)
        entry->name[j] = fatname[j];

    for (int j = 0; j < 3; j++)
        entry->ext[j] = fatname[8 + j];

    entry->attr = 0x20;
    entry->first_cluster_low = 0;
    entry->file_size = 0;

    ata_write_sector(free_sector, sector);
    return 1;
}

int fat16_mkdir(const char *dirname)
{
    if (!dirname || dirname[0] == '\0')
        return 0;

    fat16_dir_entry_t existing;
    if (fat16_find_entry(current_dir_cluster, dirname, &existing))
        return 0;

    uint16_t new_cluster = fat16_alloc_cluster();
    if (new_cluster == 0)
        return 0;

    fat16_clear_cluster(new_cluster);

    uint8_t sector[512];
    ata_read_sector(fat16_cluster_to_sector(new_cluster), sector);

    fat16_dir_entry_t *dot = (fat16_dir_entry_t *)&sector[0];
    fat16_dir_entry_t *dotdot = (fat16_dir_entry_t *)&sector[32];

    for (int i = 0; i < 32; i++)
        ((uint8_t *)dot)[i] = 0;

    for (int i = 0; i < 32; i++)
        ((uint8_t *)dotdot)[i] = 0;

    for (int i = 0; i < 11; i++)
        ((uint8_t *)dot)[i] = ' ';

    dot->name[0] = '.';
    dot->attr = 0x10;
    dot->first_cluster_low = new_cluster;

    for (int i = 0; i < 11; i++)
        ((uint8_t *)dotdot)[i] = ' ';

    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = 0x10;
    dotdot->first_cluster_low = current_dir_cluster;

    ata_write_sector(fat16_cluster_to_sector(new_cluster), sector);

    uint32_t free_sector;
    uint32_t free_offset;

    if (!fat16_find_free_dir_entry(current_dir_cluster, &free_sector, &free_offset))
        return 0;

    ata_read_sector(free_sector, sector);

    fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[free_offset];

    char fatname[11];
    fat16_format_filename(dirname, fatname);

    for (int j = 0; j < 8; j++)
        entry->name[j] = fatname[j];

    for (int j = 0; j < 3; j++)
        entry->ext[j] = fatname[8 + j];

    entry->attr = 0x10;
    entry->first_cluster_low = new_cluster;
    entry->file_size = 0;

    ata_write_sector(free_sector, sector);
    return 1;
}

/* ------------------- Delete Commands ------------------- */

int fat16_rm(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    if (!fat16_find_entry_location(current_dir_cluster, filename,
                                   &entry_sector, &entry_offset, &entry))
        return 0;

    if (entry.attr & 0x10)
        return -1;

    if (entry.first_cluster_low != 0)
        fat16_free_cluster_chain(entry.first_cluster_low);

    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    disk_entry->name[0] = 0xE5;

    ata_write_sector(entry_sector, sector);

    return 1;
}

int fat16_rmdir(const char *dirname)
{
    if (!dirname || dirname[0] == '\0')
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    if (!fat16_find_entry_location(current_dir_cluster, dirname,
                                   &entry_sector, &entry_offset, &entry))
        return 0;

    if (!(entry.attr & 0x10))
        return -1;

    uint16_t dir_cluster = entry.first_cluster_low;

    if (dir_cluster == 0)
        return 0;

    if (!fat16_is_dir_empty(dir_cluster))
        return -2;

    fat16_free_cluster_chain(dir_cluster);

    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    disk_entry->name[0] = 0xE5;

    ata_write_sector(entry_sector, sector);

    return 1;
}

/* ------------------- Extra Features ------------------- */

int fat16_ls_path(const char *path)
{
    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    uint16_t dir_cluster;
    if (!fat16_resolve_absolute(abs, &dir_cluster))
        return 0;

    uint16_t saved = current_dir_cluster;
    current_dir_cluster = dir_cluster;

    fat16_ls();

    current_dir_cluster = saved;
    return 1;
}

int fat16_mkdir_p(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    uint16_t cluster = 0;
    char part[32];
    int pi = 0;

    const char *p = abs + 1;

    for (int i = 0;; i++)
    {
        char c = p[i];

        if (c == '/' || c == '\0')
        {
            part[pi] = '\0';

            if (pi > 0)
            {
                fat16_dir_entry_t entry;

                if (fat16_find_entry(cluster, part, &entry))
                {
                    if (!(entry.attr & 0x10))
                        return 0;

                    cluster = entry.first_cluster_low;
                }
                else
                {
                    uint16_t old = current_dir_cluster;
                    current_dir_cluster = cluster;

                    if (!fat16_mkdir(part))
                    {
                        current_dir_cluster = old;
                        return 0;
                    }

                    current_dir_cluster = old;

                    if (!fat16_find_entry(cluster, part, &entry))
                        return 0;

                    cluster = entry.first_cluster_low;
                }
            }

            pi = 0;
            if (c == '\0')
                break;
        }
        else
        {
            if (pi < 31)
                part[pi++] = c;
        }
    }

    return 1;
}

int fat16_rm_rf(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    if (strcmp(abs, "/") == 0)
        return 0;

    int len = strlen(abs);
    int slash = -1;

    for (int i = len - 1; i >= 0; i--)
    {
        if (abs[i] == '/')
        {
            slash = i;
            break;
        }
    }

    if (slash == -1)
        return 0;

    char parent_path[128];
    char target_name[32];

    if (slash == 0)
        strcpy(parent_path, "/");
    else
    {
        for (int i = 0; i < slash; i++)
            parent_path[i] = abs[i];
        parent_path[slash] = '\0';
    }

    int ti = 0;
    for (int i = slash + 1; abs[i] != '\0'; i++)
    {
        if (ti < 31)
            target_name[ti++] = abs[i];
    }
    target_name[ti] = '\0';

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    if (!fat16_find_entry_location(parent_cluster, target_name,
                                   &entry_sector, &entry_offset, &entry))
        return 0;

    if (!(entry.attr & 0x10))
    {
        if (entry.first_cluster_low != 0)
            fat16_free_cluster_chain(entry.first_cluster_low);

        uint8_t sector[512];
        ata_read_sector(entry_sector, sector);

        fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
        disk_entry->name[0] = 0xE5;

        ata_write_sector(entry_sector, sector);
        return 1;
    }

    uint16_t dir_cluster = entry.first_cluster_low;

    if (dir_cluster < 2)
        return 0;

    fat16_delete_dir_recursive(dir_cluster);

    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    disk_entry->name[0] = 0xE5;

    ata_write_sector(entry_sector, sector);

    return 1;
}
