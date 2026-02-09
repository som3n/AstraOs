#include "fs/fat16.h"
#include "drivers/ata.h"
#include "vga.h"
#include "kernel/print.h"
#include "string.h"
#include "memory/kmalloc.h"

static fat16_bpb_t bpb;

static uint16_t current_dir_cluster = 0; // 0 = root
static char current_path[128] = "/";

static int fat16_read_file(const char *path, uint8_t *out_buf, uint32_t max_size, uint32_t *out_size);
static int fat16_get_file_size_internal(const char *path, uint32_t *out_size);
static int fat16_is_directory(const char *path);

/* -------------------- Helpers -------------------- */
static void fat16_copy_entry(fat16_dir_entry_t *dst, fat16_dir_entry_t *src)
{
    for (int i = 0; i < 32; i++)
        ((uint8_t *)dst)[i] = ((uint8_t *)src)[i];
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

    // FAT1
    ata_read_sector(sector_num, sector);
    *(uint16_t *)&sector[offset] = value;
    ata_write_sector(sector_num, sector);

    // FAT2 mirror
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

/* ---------------- PATH HANDLING ---------------- */

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

/* ---------------- DIRECTORY SEARCH ---------------- */

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

/* -------- Resolve path into parent dir + filename -------- */

static int fat16_split_path(const char *path, char *parent_out, char *name_out)
{
    if (!path || path[0] == '\0')
        return 0;

    int len = strlen(path);

    int slash = -1;
    for (int i = len - 1; i >= 0; i--)
    {
        if (path[i] == '/')
        {
            slash = i;
            break;
        }
    }

    if (slash == -1)
        return 0;

    if (slash == 0)
        strcpy(parent_out, "/");
    else
    {
        for (int i = 0; i < slash; i++)
            parent_out[i] = path[i];
        parent_out[slash] = '\0';
    }

    int ni = 0;
    for (int i = slash + 1; path[i] != '\0'; i++)
    {
        if (ni < 31)
            name_out[ni++] = path[i];
    }
    name_out[ni] = '\0';

    if (name_out[0] == '\0')
        return 0;

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

const char *fat16_get_path()
{
    return current_path;
}

/* ---------------- ls ---------------- */

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
                    print("  ");
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
                    print("  ");
                    print_uint(entry->file_size);
                    print(" bytes\n");
                }
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }
}

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

/* ---------------- cd ---------------- */

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

/* ---------------- cat ---------------- */

int fat16_cat(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    fat16_dir_entry_t entry;
    if (!fat16_find_entry(parent_cluster, filename, &entry))
        return 0;

    if (entry.attr & 0x10)
        return 0;

    uint32_t remaining = entry.file_size;
    uint16_t cluster = entry.first_cluster_low;
    uint8_t buf[512];

    print("\n");

    // Empty file: FAT16 commonly stores first_cluster_low = 0.
    if (remaining == 0)
        return 1;

    // Non-empty file must have a valid data cluster (>= 2).
    if (cluster < 2)
        return 0;

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

/* ---------------- touch ---------------- */

int fat16_touch(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return 0;

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

    char fatname[11];
    fat16_format_filename(filename, fatname);

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

/* ---------------- mkdir ---------------- */

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

/* ---------------- rm / rmdir ---------------- */

int fat16_rm(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    if (!fat16_find_entry_location(current_dir_cluster, filename,
                                   &entry_sector, &entry_offset, &entry))
    {
        return 0;
    }

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
                    continue;

                return 0;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

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
    {
        return 0;
    }

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

/* -------- recursive delete helper -------- */

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

                if (entry->name[0] == '.' &&
                    (entry->name[1] == ' ' || entry->name[1] == '.'))
                    continue;

                if (entry->attr & 0x10)
                {
                    uint16_t sub = entry->first_cluster_low;
                    if (sub >= 2)
                        fat16_delete_dir_recursive(sub);

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

int fat16_rm_rf(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    if (strcmp(abs, "/") == 0)
        return 0;

    char parent_path[128];
    char target_name[32];

    if (!fat16_split_path(abs, parent_path, target_name))
        return 0;

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

/* ---------------- WRITE FILE ---------------- */

int fat16_write_file(const char *path, const uint8_t *data, uint32_t size)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    uint32_t entry_sector = 0;
    uint32_t entry_offset = 0;
    fat16_dir_entry_t entry;
    int exists = fat16_find_entry_location(parent_cluster, filename,
                                           &entry_sector, &entry_offset, &entry);

    if (exists)
    {
        if (entry.attr & 0x10)
            return 0;

        if (entry.first_cluster_low != 0)
            fat16_free_cluster_chain(entry.first_cluster_low);
    }
    else
    {
        if (!fat16_find_free_dir_entry(parent_cluster, &entry_sector, &entry_offset))
            return 0;

        uint8_t secbuf[512];
        ata_read_sector(entry_sector, secbuf);

        fat16_dir_entry_t *newent = (fat16_dir_entry_t *)&secbuf[entry_offset];

        for (int i = 0; i < 32; i++)
            ((uint8_t *)newent)[i] = 0;

        char fatname[11];
        fat16_format_filename(filename, fatname);

        for (int j = 0; j < 8; j++)
            newent->name[j] = fatname[j];

        for (int j = 0; j < 3; j++)
            newent->ext[j] = fatname[8 + j];

        newent->attr = 0x20;
        newent->first_cluster_low = 0;
        newent->file_size = 0;

        ata_write_sector(entry_sector, secbuf);
    }

    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    uint32_t remaining = size;
    uint32_t written = 0;

    while (remaining > 0)
    {
        uint16_t new_cluster = fat16_alloc_cluster();
        if (new_cluster == 0)
            return 0;

        fat16_clear_cluster(new_cluster);

        if (first_cluster == 0)
            first_cluster = new_cluster;

        if (prev_cluster != 0)
            fat16_set_fat_entry(prev_cluster, new_cluster);

        fat16_set_fat_entry(new_cluster, 0xFFFF);
        prev_cluster = new_cluster;

        uint32_t sector_start = fat16_cluster_to_sector(new_cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            uint8_t buf[512];
            for (int i = 0; i < 512; i++)
                buf[i] = 0;

            for (int i = 0; i < 512 && remaining > 0; i++)
            {
                buf[i] = data[written++];
                remaining--;
            }

            ata_write_sector(sector_start + s, buf);

            if (remaining == 0)
                break;
        }
    }

    uint8_t secbuf[512];
    ata_read_sector(entry_sector, secbuf);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&secbuf[entry_offset];
    disk_entry->first_cluster_low = first_cluster;
    disk_entry->file_size = size;

    ata_write_sector(entry_sector, secbuf);

    return 1;
}

/* ---------------- APPEND FILE ---------------- */

int fat16_append_file(const char *path, const uint8_t *data, uint32_t size)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    if (!fat16_find_entry_location(parent_cluster, filename,
                                   &entry_sector, &entry_offset, &entry))
    {
        return fat16_write_file(path, data, size);
    }

    if (entry.attr & 0x10)
        return 0;

    uint32_t old_size = entry.file_size;
    uint32_t new_size = old_size + size;

    // read whole file not supported yet, so we just rewrite by simple method
    // for now: append is implemented as "rewrite + old data lost" is WRONG
    // so we must implement real append properly later

    // TEMP SIMPLE IMPLEMENTATION:
    // Just call write_file (overwrites) -> Not real append
    // return fat16_write_file(path, data, size);

    // REAL append implementation below:

    uint32_t cluster_size_bytes = bpb.sectors_per_cluster * 512;

    uint16_t cluster = entry.first_cluster_low;
    uint16_t last_cluster = cluster;

    if (cluster == 0)
    {
        // file empty, just write new data
        return fat16_write_file(path, data, size);
    }

    while (cluster < 0xFFF8)
    {
        last_cluster = cluster;
        uint16_t next = fat16_get_fat_entry(cluster);
        if (next >= 0xFFF8)
            break;
        cluster = next;
    }

    uint32_t offset_in_cluster = old_size % cluster_size_bytes;
    uint32_t remaining = size;
    uint32_t written = 0;

    // fill remaining space in last cluster
    if (offset_in_cluster != 0)
    {
        uint32_t sector_start = fat16_cluster_to_sector(last_cluster);

        uint32_t sector_index = offset_in_cluster / 512;
        uint32_t sector_offset = offset_in_cluster % 512;

        uint8_t buf[512];
        ata_read_sector(sector_start + sector_index, buf);

        for (uint32_t i = sector_offset; i < 512 && remaining > 0; i++)
        {
            buf[i] = data[written++];
            remaining--;
        }

        ata_write_sector(sector_start + sector_index, buf);

        sector_index++;

        while (sector_index < bpb.sectors_per_cluster && remaining > 0)
        {
            for (int i = 0; i < 512; i++)
                buf[i] = 0;

            for (int i = 0; i < 512 && remaining > 0; i++)
            {
                buf[i] = data[written++];
                remaining--;
            }

            ata_write_sector(sector_start + sector_index, buf);
            sector_index++;
        }
    }

    uint16_t prev = last_cluster;

    while (remaining > 0)
    {
        uint16_t new_cluster = fat16_alloc_cluster();
        if (new_cluster == 0)
            return 0;

        fat16_clear_cluster(new_cluster);

        fat16_set_fat_entry(prev, new_cluster);
        fat16_set_fat_entry(new_cluster, 0xFFFF);

        uint32_t sector_start = fat16_cluster_to_sector(new_cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            uint8_t buf[512];
            for (int i = 0; i < 512; i++)
                buf[i] = 0;

            for (int i = 0; i < 512 && remaining > 0; i++)
            {
                buf[i] = data[written++];
                remaining--;
            }

            ata_write_sector(sector_start + s, buf);

            if (remaining == 0)
                break;
        }

        prev = new_cluster;
    }

    uint8_t secbuf[512];
    ata_read_sector(entry_sector, secbuf);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&secbuf[entry_offset];
    disk_entry->file_size = new_size;

    ata_write_sector(entry_sector, secbuf);

    return 1;
}

static int fat16_get_file_size_internal(const char *path, uint32_t *out_size)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    fat16_dir_entry_t entry;
    if (!fat16_find_entry(parent_cluster, filename, &entry))
        return 0;

    if (entry.attr & 0x10)
        return 0;

    *out_size = entry.file_size;
    return 1;
}

static int fat16_is_directory(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    // root is directory
    if (strcmp(abs, "/") == 0)
        return 1;

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    fat16_dir_entry_t entry;
    if (!fat16_find_entry(parent_cluster, filename, &entry))
        return 0;

    return (entry.attr & 0x10) ? 1 : 0;
}

static int fat16_read_file(const char *path, uint8_t *out_buf, uint32_t max_size, uint32_t *out_size)
{
    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    fat16_dir_entry_t entry;
    if (!fat16_find_entry(parent_cluster, filename, &entry))
        return 0;

    if (entry.attr & 0x10)
        return 0;

    uint32_t remaining = entry.file_size;

    if (remaining > max_size)
        return 0;

    uint16_t cluster = entry.first_cluster_low;
    uint32_t read = 0;

    if (remaining == 0)
    {
        *out_size = 0;
        return 1;
    }

    // Non-empty file must have a valid data cluster (>= 2).
    if (cluster < 2)
        return 0;

    uint8_t sector[512];

    while (cluster < 0xFFF8)
    {
        uint32_t sector_start = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            ata_read_sector(sector_start + s, sector);

            for (int i = 0; i < 512; i++)
            {
                if (remaining == 0)
                {
                    *out_size = read;
                    return 1;
                }

                out_buf[read++] = sector[i];
                remaining--;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    *out_size = read;
    return 1;
}

/* ---------------- COPY + MOVE ---------------- */

int fat16_cp(const char *src, const char *dst)
{
    if (!src || !dst)
        return 0;

    if (fat16_is_directory(src))
        return 0; // directory copy not supported yet

    uint32_t file_size;
    if (!fat16_get_file_size_internal(src, &file_size))
        return 0;

    uint8_t *buf = (uint8_t *)kmalloc(file_size + 4);
    if (!buf)
        return 0;

    uint32_t read_size;
    if (!fat16_read_file(src, buf, file_size + 4, &read_size))
    {
        kfree(buf);
        return 0;
    }

    if (read_size != file_size)
    {
        kfree(buf);
        return 0;
    }

    int result = 0;

    // If dst is a directory, copy inside it
    if (fat16_is_directory(dst))
    {
        char abs_dst[128];
        fat16_normalize_path(current_path, dst, abs_dst);

        char abs_src[128];
        fat16_normalize_path(current_path, src, abs_src);

        char parent_src[128];
        char src_name[32];

        if (!fat16_split_path(abs_src, parent_src, src_name))
        {
            kfree(buf);
            return 0;
        }

        char final_dst[128];
        strcpy(final_dst, abs_dst);

        if (strcmp(final_dst, "/") != 0)
            strcat(final_dst, "/");

        strcat(final_dst, src_name);

        result = fat16_write_file(final_dst, buf, file_size);
    }
    else
    {
        result = fat16_write_file(dst, buf, file_size);
    }

    kfree(buf);
    return result;
}

int fat16_mv(const char *src, const char *dst)
{
    if (!src || !dst)
        return 0;

    if (fat16_is_directory(src))
        return 0; // directory mv not supported yet

    char abs_src[128];
    fat16_normalize_path(current_path, src, abs_src);

    // split src -> parent + filename
    char src_parent[128];
    char src_name[32];

    if (!fat16_split_path(abs_src, src_parent, src_name))
        return 0;

    uint16_t src_parent_cluster;
    if (!fat16_resolve_absolute(src_parent, &src_parent_cluster))
        return 0;

    // find src entry location
    uint32_t src_sector;
    uint32_t src_offset;
    fat16_dir_entry_t src_entry;

    if (!fat16_find_entry_location(src_parent_cluster, src_name,
                                   &src_sector, &src_offset, &src_entry))
        return 0;

    if (src_entry.attr & 0x10)
        return 0;

    // normalize destination
    char abs_dst[128];
    fat16_normalize_path(current_path, dst, abs_dst);

    // if dst is directory -> put file inside dst
    if (fat16_is_directory(abs_dst))
    {
        char final_dst[128];
        strcpy(final_dst, abs_dst);

        if (strcmp(final_dst, "/") != 0)
            strcat(final_dst, "/");

        strcat(final_dst, src_name);

        strcpy(abs_dst, final_dst);
    }

    // split dst -> parent + new filename
    char dst_parent[128];
    char dst_name[32];

    if (!fat16_split_path(abs_dst, dst_parent, dst_name))
        return 0;

    uint16_t dst_parent_cluster;
    if (!fat16_resolve_absolute(dst_parent, &dst_parent_cluster))
        return 0;

    // check if dst already exists
    fat16_dir_entry_t existing;
    if (fat16_find_entry(dst_parent_cluster, dst_name, &existing))
        return 0; // destination already exists

    // find free entry in dst parent
    uint32_t free_sector;
    uint32_t free_offset;

    if (!fat16_find_free_dir_entry(dst_parent_cluster, &free_sector, &free_offset))
        return 0;

    // write new entry into destination directory
    uint8_t buf[512];
    ata_read_sector(free_sector, buf);

    fat16_dir_entry_t *new_entry = (fat16_dir_entry_t *)&buf[free_offset];

    fat16_copy_entry(new_entry, &src_entry);

    // update filename
    char fatname[11];
    fat16_format_filename(dst_name, fatname);

    for (int j = 0; j < 8; j++)
        new_entry->name[j] = fatname[j];

    for (int j = 0; j < 3; j++)
        new_entry->ext[j] = fatname[8 + j];

    ata_write_sector(free_sector, buf);

    // delete old entry
    uint8_t secbuf[512];
    ata_read_sector(src_sector, secbuf);

    fat16_dir_entry_t *old_entry = (fat16_dir_entry_t *)&secbuf[src_offset];
    old_entry->name[0] = 0xE5;

    ata_write_sector(src_sector, secbuf);

    return 1;
}

int fat16_filesize(const char *path, uint32_t *out_size)
{
    return fat16_get_file_size_internal(path, out_size);
}

int fat16_list_dir(const char *path, char *out, uint32_t out_size, uint32_t *out_written)
{
    if (!out || out_size == 0 || !out_written)
        return 0;

    out[0] = '\0';
    *out_written = 0;

    if (!path || path[0] == '\0')
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    uint16_t dir_cluster;
    if (!fat16_resolve_absolute(abs, &dir_cluster))
        return 0;

    uint8_t sector[512];
    uint32_t written = 0;

    // Append "name\n" if it fits; keeps output NUL terminated.
    #define APPEND_LINE(name_buf)                                                     \
        do                                                                            \
        {                                                                             \
            const char *s__ = (name_buf);                                             \
            uint32_t i__ = 0;                                                         \
            while (s__[i__] != '\0')                                                  \
            {                                                                         \
                if (written + 2 >= out_size)                                          \
                    goto done;                                                        \
                out[written++] = s__[i__++];                                          \
            }                                                                         \
            if (written + 2 >= out_size)                                              \
                goto done;                                                            \
            out[written++] = '\n';                                                    \
            out[written] = '\0';                                                      \
        } while (0)

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
                    goto done;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                char filename[13];
                fat16_entry_to_name(entry, filename);
                APPEND_LINE(filename);
            }
        }

        goto done;
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
                    goto done;
                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;
                if (entry->attr == 0x0F)
                    continue;

                char filename[13];
                fat16_entry_to_name(entry, filename);
                APPEND_LINE(filename);
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

done:
    #undef APPEND_LINE
    *out_written = written;
    return 1;
}

int fat16_read_at(const char *path, uint32_t offset, uint8_t *out, uint32_t len, uint32_t *out_read)
{
    if (!out_read)
        return 0;
    *out_read = 0;

    if (!path || path[0] == '\0')
        return 0;

    // len==0 is allowed (existence check)
    if (len > 0 && !out)
        return 0;

    char abs[128];
    fat16_normalize_path(current_path, path, abs);

    // root is a directory, not a readable file
    if (strcmp(abs, "/") == 0)
        return 0;

    char parent_path[128];
    char filename[32];

    if (!fat16_split_path(abs, parent_path, filename))
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_absolute(parent_path, &parent_cluster))
        return 0;

    fat16_dir_entry_t entry;
    if (!fat16_find_entry(parent_cluster, filename, &entry))
        return 0;

    if (entry.attr & 0x10)
        return 0;

    uint32_t file_size = entry.file_size;
    if (offset >= file_size)
    {
        *out_read = 0;
        return 1;
    }

    uint32_t remaining = file_size - offset;
    if (len < remaining)
        remaining = len;

    if (remaining == 0)
    {
        *out_read = 0;
        return 1;
    }

    uint16_t cluster = entry.first_cluster_low;
    if (cluster < 2)
        return 0;

    uint32_t cluster_size_bytes = (uint32_t)bpb.sectors_per_cluster * 512;

    // Skip clusters until we reach the offset.
    uint32_t skip = offset;
    while (skip >= cluster_size_bytes)
    {
        uint16_t next = fat16_get_fat_entry(cluster);
        if (next >= 0xFFF8)
            return 0;
        cluster = next;
        skip -= cluster_size_bytes;
    }

    uint32_t copied = 0;
    uint8_t sector[512];

    while (cluster < 0xFFF8 && remaining > 0)
    {
        uint32_t sector_start = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster && remaining > 0; s++)
        {
            ata_read_sector(sector_start + (uint32_t)s, sector);

            uint32_t start_i = 0;
            if (skip > 0)
            {
                start_i = skip;
                if (start_i >= 512)
                {
                    skip -= 512;
                    continue;
                }
                skip = 0;
            }

            for (uint32_t i = start_i; i < 512 && remaining > 0; i++)
            {
                out[copied++] = sector[i];
                remaining--;
            }
        }

        if (remaining == 0)
            break;

        cluster = fat16_get_fat_entry(cluster);
    }

    *out_read = copied;
    return 1;
}
