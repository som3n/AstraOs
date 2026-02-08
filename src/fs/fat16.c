#include "fs/fat16.h"
#include "drivers/ata.h"
#include "vga.h"
#include "kernel/print.h"
#include "string.h"
#include "memory/kmalloc.h"

static fat16_bpb_t bpb;

static uint16_t current_dir_cluster = 0; // 0 = root
static char current_path[128] = "/";

/* ----------- Forward Declarations (IMPORTANT) ----------- */
static uint16_t fat16_get_parent_cluster(uint16_t dir_cluster);
static int fat16_find_free_dir_entry(uint16_t dir_cluster, uint32_t *out_sector, uint32_t *out_offset);

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

    // ROOT special case (root cannot expand in FAT16)
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

        return 0; // root full
    }

    // SUBDIRECTORY (cluster chain)
    uint16_t cluster = dir_cluster;
    uint16_t last_cluster = dir_cluster;

    while (cluster < 0xFFF8)
    {
        last_cluster = cluster;

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

    // NO FREE SLOT FOUND -> EXPAND DIRECTORY by allocating new cluster
    uint16_t new_cluster = fat16_alloc_cluster();
    if (new_cluster == 0)
        return 0;

    // link last cluster -> new cluster
    fat16_set_fat_entry(last_cluster, new_cluster);
    fat16_set_fat_entry(new_cluster, 0xFFFF);

    // clear the new cluster (empty directory entries)
    fat16_clear_cluster(new_cluster);

    // return first entry slot of new cluster
    uint32_t new_sector = fat16_cluster_to_sector(new_cluster);

    *out_sector = new_sector;
    *out_offset = 0;

    return 1;
}

static void fat16_free_cluster_chain(uint16_t start_cluster)
{
    uint16_t cluster = start_cluster;

    while (cluster >= 2 && cluster < 0xFFF8)
    {
        uint16_t next = fat16_get_fat_entry(cluster);

        // mark cluster as free
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

    // ROOT directory special case
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
                    if (out_sector)
                        *out_sector = root_start + s;
                    if (out_offset)
                        *out_offset = i;
                    if (out_entry)
                        *out_entry = *entry;
                    return 1;
                }
            }
        }

        return 0;
    }

    // SUBDIRECTORY
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
                    if (out_sector)
                        *out_sector = start_sector + s;
                    if (out_offset)
                        *out_offset = i;
                    if (out_entry)
                        *out_entry = *entry;
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
                    return 1; // no more entries

                if ((uint8_t)entry->name[0] == 0xE5)
                    continue;

                if (entry->attr == 0x0F)
                    continue;

                // allow "." and ".."
                if (entry->name[0] == '.' &&
                    (entry->name[1] == ' ' || entry->name[1] == '.'))
                {
                    continue;
                }

                // something else exists -> not empty
                return 0;
            }
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 1;
}

static void fat16_write_cluster(uint16_t cluster, const uint8_t *data, uint32_t size)
{
    uint32_t start_sector = fat16_cluster_to_sector(cluster);

    uint8_t buf[512];
    uint32_t written = 0;

    for (int s = 0; s < bpb.sectors_per_cluster; s++)
    {
        for (int i = 0; i < 512; i++)
            buf[i] = 0;

        for (int i = 0; i < 512 && written < size; i++)
        {
            buf[i] = data[written];
            written++;
        }

        ata_write_sector(start_sector + s, buf);

        if (written >= size)
            return;
    }
}

static uint32_t fat16_cluster_size_bytes()
{
    return bpb.sectors_per_cluster * 512;
}

static uint16_t fat16_get_last_cluster(uint16_t start_cluster)
{
    if (start_cluster == 0)
        return 0;

    uint16_t cluster = start_cluster;
    uint16_t next = fat16_get_fat_entry(cluster);

    while (next < 0xFFF8)
    {
        cluster = next;
        next = fat16_get_fat_entry(cluster);
    }

    return cluster;
}

static void fat16_write_cluster_offset(uint16_t cluster, uint32_t offset, const uint8_t *data, uint32_t size)
{
    uint32_t start_sector = fat16_cluster_to_sector(cluster);

    uint32_t cluster_bytes = bpb.sectors_per_cluster * 512;
    if (offset >= cluster_bytes)
        return;

    uint8_t buf[512];

    uint32_t written = 0;
    uint32_t byte_pos = 0;

    for (int s = 0; s < bpb.sectors_per_cluster; s++)
    {
        ata_read_sector(start_sector + s, buf);

        for (int i = 0; i < 512; i++)
        {
            if (byte_pos >= offset && written < size)
            {
                buf[i] = data[written];
                written++;
            }

            byte_pos++;

            if (written >= size)
                break;
        }

        ata_write_sector(start_sector + s, buf);

        if (written >= size)
            return;
    }
}

static int fat16_resolve_dir_cluster(const char *path, uint16_t *out_cluster)
{
    if (!path || path[0] == '\0')
        return 0;

    uint16_t cluster;

    // absolute path starts from root
    if (path[0] == '/')
        cluster = 0;
    else
        cluster = current_dir_cluster;

    int i = 0;

    // skip leading '/'
    while (path[i] == '/')
        i++;

    char part[64];
    int part_len = 0;

    while (1)
    {
        char c = path[i];

        if (c == '/' || c == '\0')
        {
            part[part_len] = '\0';

            if (part_len > 0)
            {
                // handle "."
                if (strcmp(part, ".") == 0)
                {
                    // do nothing
                }
                else if (strcmp(part, "..") == 0)
                {
                    if (cluster == 0)
                        cluster = 0;
                    else
                        cluster = fat16_get_parent_cluster(cluster);
                }
                else
                {
                    fat16_dir_entry_t entry;
                    if (!fat16_find_entry(cluster, part, &entry))
                        return 0;

                    if (!(entry.attr & 0x10))
                        return 0;

                    cluster = entry.first_cluster_low;
                }
            }

            part_len = 0;

            if (c == '\0')
                break;
        }
        else
        {
            if (part_len < 63)
                part[part_len++] = c;
        }

        i++;
    }

    *out_cluster = cluster;
    return 1;
}

static int fat16_mkdir_in_cluster(uint16_t parent_cluster, const char *dirname)
{
    if (!dirname || dirname[0] == '\0')
        return 0;

    fat16_dir_entry_t existing;
    if (fat16_find_entry(parent_cluster, dirname, &existing))
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
    dot->file_size = 0;

    for (int i = 0; i < 11; i++)
        ((uint8_t *)dotdot)[i] = ' ';

    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
    dotdot->attr = 0x10;
    dotdot->first_cluster_low = parent_cluster;
    dotdot->file_size = 0;

    ata_write_sector(fat16_cluster_to_sector(new_cluster), sector);

    uint32_t free_sector;
    uint32_t free_offset;

    if (!fat16_find_free_dir_entry(parent_cluster, &free_sector, &free_offset))
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
    entry->reserved = 0;
    entry->creation_time_tenths = 0;
    entry->creation_time = 0;
    entry->creation_date = 0;
    entry->last_access_date = 0;
    entry->first_cluster_high = 0;
    entry->write_time = 0;
    entry->write_date = 0;
    entry->first_cluster_low = new_cluster;
    entry->file_size = 0;

    ata_write_sector(free_sector, sector);

    return 1;
}

static void fat16_normalize_path(char *path)
{
    if (!path || path[0] == '\0')
        return;

    // always absolute path in our system
    if (path[0] != '/')
        return;

    char temp[128];
    int temp_len = 0;

    int i = 0;

    // stack of positions where each directory starts
    int stack[32];
    int top = 0;

    // always start with root
    temp[temp_len++] = '/';
    stack[top++] = 1;

    while (path[i] != '\0')
    {
        // skip multiple slashes
        while (path[i] == '/')
            i++;

        if (path[i] == '\0')
            break;

        // read one part
        char part[64];
        int part_len = 0;

        while (path[i] != '\0' && path[i] != '/' && part_len < 63)
        {
            part[part_len++] = path[i++];
        }
        part[part_len] = '\0';

        // ignore "."
        if (strcmp(part, ".") == 0)
        {
            continue;
        }

        // handle ".."
        if (strcmp(part, "..") == 0)
        {
            if (top > 1)
            {
                // remove last directory
                top--;
                temp_len = stack[top - 1];
                temp[temp_len] = '\0';
            }
            continue;
        }

        // add normal directory
        if (temp_len > 1 && temp[temp_len - 1] != '/')
            temp[temp_len++] = '/';

        int start_pos = temp_len;

        for (int j = 0; part[j] != '\0'; j++)
        {
            if (temp_len < 127)
                temp[temp_len++] = part[j];
        }

        temp[temp_len] = '\0';

        if (top < 32)
            stack[top++] = start_pos;
    }

    // if empty -> root
    if (temp_len == 0)
    {
        strcpy(path, "/");
        return;
    }

    // remove trailing slash except root
    if (temp_len > 1 && temp[temp_len - 1] == '/')
    {
        temp[temp_len - 1] = '\0';
    }

    strcpy(path, temp);
}

static int fat16_split_path(const char *path, char *parent_out, char *name_out)
{
    if (!path || path[0] == '\0')
        return 0;

    int len = strlen(path);
    int last_slash = -1;

    for (int i = 0; i < len; i++)
    {
        if (path[i] == '/')
            last_slash = i;
    }

    if (last_slash == -1)
    {
        strcpy(parent_out, "");
        strcpy(name_out, path);
        return 1;
    }

    // parent
    if (last_slash == 0)
    {
        parent_out[0] = '/';
        parent_out[1] = '\0';
    }
    else
    {
        for (int i = 0; i < last_slash; i++)
            parent_out[i] = path[i];

        parent_out[last_slash] = '\0';
    }

    // name
    int j = 0;
    for (int i = last_slash + 1; i < len; i++)
        name_out[j++] = path[i];

    name_out[j] = '\0';

    return (name_out[0] != '\0');
}

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
                    return 1; // end of entries

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

                // if directory -> recurse
                if (entry->attr & 0x10)
                {
                    uint16_t sub_cluster = entry->first_cluster_low;

                    if (sub_cluster != 0)
                    {
                        fat16_delete_dir_recursive(sub_cluster);
                        fat16_free_cluster_chain(sub_cluster);
                    }
                }
                else
                {
                    // normal file
                    if (entry->first_cluster_low != 0)
                        fat16_free_cluster_chain(entry->first_cluster_low);
                }

                // mark entry deleted
                entry->name[0] = 0xE5;
            }

            // write updated sector back
            ata_write_sector(start_sector + s, sector);
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    return 1;
}

static int fat16_resolve_entry(const char *path, fat16_dir_entry_t *out_entry, uint16_t *out_parent_cluster)
{
    if (!path || path[0] == '\0')
        return 0;

    uint16_t cluster;

    if (path[0] == '/')
        cluster = 0;
    else
        cluster = current_dir_cluster;

    int i = 0;
    while (path[i] == '/')
        i++;

    char part[64];
    int part_len = 0;

    fat16_dir_entry_t entry;
    int found_any = 0;

    while (1)
    {
        char c = path[i];

        if (c == '/' || c == '\0')
        {
            part[part_len] = '\0';

            if (part_len > 0)
            {
                if (strcmp(part, ".") == 0)
                {
                    // do nothing
                }
                else if (strcmp(part, "..") == 0)
                {
                    if (cluster == 0)
                        cluster = 0;
                    else
                        cluster = fat16_get_parent_cluster(cluster);
                }
                else
                {
                    if (!fat16_find_entry(cluster, part, &entry))
                        return 0;

                    found_any = 1;

                    if (out_parent_cluster)
                        *out_parent_cluster = cluster;

                    if (c == '\0')
                    {
                        if (out_entry)
                            *out_entry = entry;
                        return 1;
                    }

                    if (!(entry.attr & 0x10))
                        return 0;

                    cluster = entry.first_cluster_low;
                }
            }

            part_len = 0;

            if (c == '\0')
                break;
        }
        else
        {
            if (part_len < 63)
                part[part_len++] = c;
        }

        i++;
    }

    // if path was "/" or something like that
    if (!found_any)
        return 0;

    return 0;
}

static int fat16_get_cluster_from_path(const char *path, uint16_t *out_cluster)
{
    if (!path || path[0] == '\0')
    {
        *out_cluster = current_dir_cluster;
        return 1;
    }

    if (strcmp(path, "/") == 0)
    {
        *out_cluster = 0;
        return 1;
    }

    return fat16_resolve_dir_cluster(path, out_cluster);
}

static int fat16_is_directory_path(const char *path)
{
    uint16_t temp;
    return fat16_resolve_dir_cluster(path, &temp);
}

static int fat16_is_directory(uint16_t parent_cluster, const char *name)
{
    fat16_dir_entry_t e;
    if (!fat16_find_entry(parent_cluster, name, &e))
        return 0;

    return (e.attr & 0x10) ? 1 : 0;
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

int fat16_cd(const char *dirname)
{
    if (!dirname || dirname[0] == '\0')
        return 0;

    // go root
    if (strcmp(dirname, "/") == 0)
    {
        current_dir_cluster = 0;
        strcpy(current_path, "/");
        return 1;
    }

    // resolve cluster using path resolver (supports ../.. and dir/subdir)
    uint16_t target_cluster;
    if (!fat16_resolve_dir_cluster(dirname, &target_cluster))
        return 0;

    current_dir_cluster = target_cluster;

    // update current_path string
    if (dirname[0] == '/')
    {
        strcpy(current_path, dirname);
    }
    else
    {
        if (strcmp(current_path, "/") != 0)
            strcat(current_path, "/");

        strcat(current_path, dirname);
    }

    // normalize the path so pwd becomes clean
    fat16_normalize_path(current_path);

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

char *strcat(char *dest, const char *src)
{
    int i = 0;
    int j = 0;

    while (dest[i] != '\0')
        i++;

    while (src[j] != '\0')
        dest[i++] = src[j++];

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

int fat16_touch(const char *filename)
{
    if (!filename || filename[0] == '\0')
        return 0;

    char parent_path[128];
    char final_name[64];

    if (!fat16_split_path(filename, parent_path, final_name))
        return 0;

    uint16_t target_cluster;

    // if no parent path, use current directory
    if (parent_path[0] == '\0')
        target_cluster = current_dir_cluster;
    else
    {
        if (!fat16_resolve_dir_cluster(parent_path, &target_cluster))
            return 0;
    }

    // prevent touching if name empty
    if (final_name[0] == '\0')
        return 0;

    // check if already exists
    fat16_dir_entry_t existing;
    if (fat16_find_entry(target_cluster, final_name, &existing))
        return 0;

    char fatname[11];
    fat16_format_filename(final_name, fatname);

    uint32_t free_sector;
    uint32_t free_offset;

    if (!fat16_find_free_dir_entry(target_cluster, &free_sector, &free_offset))
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

    entry->attr = 0x20; // normal file
    entry->reserved = 0;

    entry->creation_time_tenths = 0;
    entry->creation_time = 0;
    entry->creation_date = 0;

    entry->last_access_date = 0;
    entry->first_cluster_high = 0;

    entry->write_time = 0;
    entry->write_date = 0;

    entry->first_cluster_low = 0;
    entry->file_size = 0;

    ata_write_sector(free_sector, sector);

    return 1;
}

int fat16_mkdir(const char *dirname)
{
    if (!dirname || dirname[0] == '\0')
        return 0;

    // split into parent path + final name
    char parent_path[128];
    char final_name[64];

    int len = strlen(dirname);
    int last_slash = -1;

    for (int i = 0; i < len; i++)
    {
        if (dirname[i] == '/')
            last_slash = i;
    }

    if (last_slash == -1)
    {
        // no slash: create in current dir
        return fat16_mkdir_in_cluster(current_dir_cluster, dirname);
    }

    // copy parent path
    for (int i = 0; i < last_slash; i++)
        parent_path[i] = dirname[i];
    parent_path[last_slash] = '\0';

    // copy final name
    int j = 0;
    for (int i = last_slash + 1; i < len; i++)
        final_name[j++] = dirname[i];
    final_name[j] = '\0';

    if (final_name[0] == '\0')
        return 0;

    uint16_t parent_cluster;
    if (!fat16_resolve_dir_cluster(parent_path, &parent_cluster))
        return 0;

    return fat16_mkdir_in_cluster(parent_cluster, final_name);
}

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
        return 0; // not found
    }

    // cannot rm directory
    if (entry.attr & 0x10)
        return -1;

    // free file cluster chain if any
    if (entry.first_cluster_low != 0)
        fat16_free_cluster_chain(entry.first_cluster_low);

    // mark directory entry as deleted
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

    // find directory entry in current directory
    if (!fat16_find_entry_location(current_dir_cluster, dirname,
                                   &entry_sector, &entry_offset, &entry))
    {
        return 0; // not found
    }

    // must be directory
    if (!(entry.attr & 0x10))
        return -1; // not a directory

    uint16_t dir_cluster = entry.first_cluster_low;

    // root cannot be removed
    if (dir_cluster == 0)
        return 0;

    // check empty
    if (!fat16_is_dir_empty(dir_cluster))
        return -2; // directory not empty

    // free cluster chain
    fat16_free_cluster_chain(dir_cluster);

    // delete directory entry from parent directory
    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    disk_entry->name[0] = 0xE5;

    ata_write_sector(entry_sector, sector);

    return 1;
}

int fat16_write(const char *filename, const uint8_t *data, uint32_t size)
{
    if (!filename || filename[0] == '\0')
        return 0;

    if (!data && size > 0)
        return 0;

    // if file exists, delete old chain first
    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    int exists = fat16_find_entry_location(current_dir_cluster, filename,
                                           &entry_sector, &entry_offset, &entry);

    if (exists)
    {
        // cannot write to directory
        if (entry.attr & 0x10)
            return -1;

        // free old cluster chain
        if (entry.first_cluster_low != 0)
            fat16_free_cluster_chain(entry.first_cluster_low);
    }
    else
    {
        // create new entry slot
        if (!fat16_find_free_dir_entry(current_dir_cluster, &entry_sector, &entry_offset))
            return 0;

        // initialize empty entry
        for (int i = 0; i < 32; i++)
            ((uint8_t *)&entry)[i] = 0;

        char fatname[11];
        fat16_format_filename(filename, fatname);

        for (int j = 0; j < 8; j++)
            entry.name[j] = fatname[j];
        for (int j = 0; j < 3; j++)
            entry.ext[j] = fatname[8 + j];

        entry.attr = 0x20;
    }

    // allocate cluster chain
    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    uint32_t remaining = size;
    uint32_t offset = 0;

    uint32_t cluster_bytes = fat16_cluster_size_bytes();

    while (remaining > 0)
    {
        uint16_t new_cluster = fat16_alloc_cluster();
        if (new_cluster == 0)
            return 0;

        if (first_cluster == 0)
            first_cluster = new_cluster;

        if (prev_cluster != 0)
            fat16_set_fat_entry(prev_cluster, new_cluster);

        fat16_set_fat_entry(new_cluster, 0xFFFF);

        uint32_t chunk = (remaining > cluster_bytes) ? cluster_bytes : remaining;

        fat16_write_cluster(new_cluster, data + offset, chunk);

        offset += chunk;
        remaining -= chunk;

        prev_cluster = new_cluster;
    }

    entry.first_cluster_low = first_cluster;
    entry.file_size = size;

    // write entry back to disk
    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    *disk_entry = entry;

    ata_write_sector(entry_sector, sector);

    return 1;
}

int fat16_append(const char *filename, const uint8_t *data, uint32_t size)
{
    if (!filename || filename[0] == '\0')
        return 0;

    if (!data || size == 0)
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    // must exist
    if (!fat16_find_entry_location(current_dir_cluster, filename,
                                   &entry_sector, &entry_offset, &entry))
    {
        return 0;
    }

    // cannot append to directory
    if (entry.attr & 0x10)
        return -1;

    uint32_t cluster_bytes = bpb.sectors_per_cluster * 512;

    // if empty file, just write normally
    if (entry.first_cluster_low == 0)
    {
        return fat16_write(filename, data, size);
    }

    uint16_t last_cluster = fat16_get_last_cluster(entry.first_cluster_low);

    // offset inside last cluster
    uint32_t offset_in_cluster = entry.file_size % cluster_bytes;

    uint32_t remaining = size;
    uint32_t written_total = 0;

    // write into last cluster free space first
    if (offset_in_cluster < cluster_bytes)
    {
        uint32_t free_space = cluster_bytes - offset_in_cluster;
        uint32_t chunk = (remaining > free_space) ? free_space : remaining;

        fat16_write_cluster_offset(last_cluster, offset_in_cluster, data, chunk);

        remaining -= chunk;
        written_total += chunk;
    }

    // allocate new clusters if needed
    uint16_t prev = last_cluster;

    while (remaining > 0)
    {
        uint16_t new_cluster = fat16_alloc_cluster();
        if (new_cluster == 0)
            return 0;

        fat16_set_fat_entry(prev, new_cluster);
        fat16_set_fat_entry(new_cluster, 0xFFFF);

        uint32_t chunk = (remaining > cluster_bytes) ? cluster_bytes : remaining;

        fat16_write_cluster(new_cluster, data + written_total, chunk);

        remaining -= chunk;
        written_total += chunk;

        prev = new_cluster;
    }

    // update size in directory entry
    entry.file_size += size;

    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    *disk_entry = entry;

    ata_write_sector(entry_sector, sector);

    return 1;
}

int fat16_mv(const char *oldname, const char *newname)
{
    if (!oldname || oldname[0] == '\0')
        return 0;

    if (!newname || newname[0] == '\0')
        return 0;

    // find old entry location
    uint32_t old_sector;
    uint32_t old_offset;
    fat16_dir_entry_t old_entry;

    if (!fat16_find_entry_location(current_dir_cluster, oldname,
                                   &old_sector, &old_offset, &old_entry))
    {
        return 0; // old file not found
    }

    // check if new name already exists
    fat16_dir_entry_t temp;
    if (fat16_find_entry(current_dir_cluster, newname, &temp))
    {
        return -1; // new already exists
    }

    // format new filename
    char fatname[11];
    fat16_format_filename(newname, fatname);

    // load sector and edit entry directly
    uint8_t sector[512];
    ata_read_sector(old_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[old_offset];

    for (int j = 0; j < 8; j++)
        disk_entry->name[j] = fatname[j];

    for (int j = 0; j < 3; j++)
        disk_entry->ext[j] = fatname[8 + j];

    ata_write_sector(old_sector, sector);

    return 1;
}

int fat16_cp(const char *src, const char *dst)
{
    if (!src || src[0] == '\0')
        return 0;

    if (!dst || dst[0] == '\0')
        return 0;

    char src_parent_path[128];
    char src_name[64];

    if (!fat16_split_path(src, src_parent_path, src_name))
        return 0;

    uint16_t src_parent_cluster;
    if (src_parent_path[0] == '\0')
        src_parent_cluster = current_dir_cluster;
    else
    {
        if (!fat16_resolve_dir_cluster(src_parent_path, &src_parent_cluster))
            return 0;
    }

    fat16_dir_entry_t src_entry;
    if (!fat16_find_entry(src_parent_cluster, src_name, &src_entry))
        return 0;

    if (src_entry.attr & 0x10)
        return -1; // source is directory

    // destination split
    char dst_parent_path[128];
    char dst_name[64];

    if (!fat16_split_path(dst, dst_parent_path, dst_name))
        return 0;

    uint16_t dst_parent_cluster;
    if (dst_parent_path[0] == '\0')
        dst_parent_cluster = current_dir_cluster;
    else
    {
        if (!fat16_resolve_dir_cluster(dst_parent_path, &dst_parent_cluster))
            return 0;
    }

    // if destination ends with "/" then treat it as directory
    int dst_len = strlen(dst);
    if (dst_len > 0 && dst[dst_len - 1] == '/')
    {
        dst_parent_cluster = 0;
        if (!fat16_resolve_dir_cluster(dst, &dst_parent_cluster))
            return 0;

        strcpy(dst_name, src_name);
    }
    else
    {
        // if dst exists and is directory, copy into it
        fat16_dir_entry_t dst_check;
        if (fat16_find_entry(dst_parent_cluster, dst_name, &dst_check))
        {
            if (dst_check.attr & 0x10)
            {
                dst_parent_cluster = dst_check.first_cluster_low;
                strcpy(dst_name, src_name);
            }
            else
            {
                return -2; // destination file already exists
            }
        }
    }

    // destination must not already exist
    fat16_dir_entry_t existing;
    if (fat16_find_entry(dst_parent_cluster, dst_name, &existing))
        return -2;

    // empty file case
    if (src_entry.file_size == 0 || src_entry.first_cluster_low == 0)
    {
        // create file inside dst_parent_cluster manually
        uint32_t free_sector, free_offset;
        if (!fat16_find_free_dir_entry(dst_parent_cluster, &free_sector, &free_offset))
            return 0;

        uint8_t sector[512];
        ata_read_sector(free_sector, sector);

        fat16_dir_entry_t *entry = (fat16_dir_entry_t *)&sector[free_offset];

        for (int i = 0; i < 32; i++)
            ((uint8_t *)entry)[i] = 0;

        char fatname[11];
        fat16_format_filename(dst_name, fatname);

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

    // allocate buffer
    uint32_t size = src_entry.file_size;
    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf)
        return 0;

    // read source
    uint32_t remaining = size;
    uint32_t offset = 0;
    uint16_t cluster = src_entry.first_cluster_low;

    uint8_t sector[512];

    while (cluster < 0xFFF8 && remaining > 0)
    {
        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            ata_read_sector(start_sector + s, sector);

            for (int i = 0; i < 512 && remaining > 0; i++)
            {
                buf[offset++] = sector[i];
                remaining--;
            }

            if (remaining == 0)
                break;
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    // temporarily change current_dir_cluster to write into correct directory
    uint16_t old_cluster = current_dir_cluster;
    current_dir_cluster = dst_parent_cluster;

    int result = fat16_write(dst_name, buf, size);

    current_dir_cluster = old_cluster;

    return result;
}

int fat16_mkdir_p(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    uint16_t cluster;

    // absolute starts from root
    if (path[0] == '/')
        cluster = 0;
    else
        cluster = current_dir_cluster;

    int i = 0;

    while (path[i] == '/')
        i++;

    char part[64];
    int part_len = 0;

    while (1)
    {
        char c = path[i];

        if (c == '/' || c == '\0')
        {
            part[part_len] = '\0';

            if (part_len > 0)
            {
                fat16_dir_entry_t entry;

                if (fat16_find_entry(cluster, part, &entry))
                {
                    // exists, must be directory
                    if (!(entry.attr & 0x10))
                        return 0;

                    cluster = entry.first_cluster_low;
                }
                else
                {
                    // create missing directory
                    if (!fat16_mkdir_in_cluster(cluster, part))
                        return 0;

                    // now find it again and move into it
                    if (!fat16_find_entry(cluster, part, &entry))
                        return 0;

                    cluster = entry.first_cluster_low;
                }
            }

            part_len = 0;

            if (c == '\0')
                break;
        }
        else
        {
            if (part_len < 63)
                part[part_len++] = c;
        }

        i++;
    }

    return 1;
}

int fat16_rmdir_r(const char *dirname)
{
    if (!dirname || dirname[0] == '\0')
        return 0;

    uint32_t entry_sector;
    uint32_t entry_offset;
    fat16_dir_entry_t entry;

    if (!fat16_find_entry_location(current_dir_cluster, dirname,
                                   &entry_sector, &entry_offset, &entry))
    {
        return 0; // not found
    }

    if (!(entry.attr & 0x10))
        return -1; // not a directory

    uint16_t dir_cluster = entry.first_cluster_low;

    if (dir_cluster == 0)
        return 0; // cannot delete root

    // recursively delete everything inside
    fat16_delete_dir_recursive(dir_cluster);

    // free cluster chain of the directory itself
    fat16_free_cluster_chain(dir_cluster);

    // delete the directory entry from parent
    uint8_t sector[512];
    ata_read_sector(entry_sector, sector);

    fat16_dir_entry_t *disk_entry = (fat16_dir_entry_t *)&sector[entry_offset];
    disk_entry->name[0] = 0xE5;

    ata_write_sector(entry_sector, sector);

    return 1;
}

int fat16_cd_path(const char *path)
{
    if (!path || path[0] == '\0')
        return 0;

    uint16_t cluster;
    if (!fat16_resolve_dir_cluster(path, &cluster))
        return 0;

    current_dir_cluster = cluster;

    char new_path[128];

    if (path[0] == '/')
    {
        strcpy(new_path, path);
    }
    else
    {
        strcpy(new_path, current_path);

        if (strcmp(new_path, "/") != 0)
            strcat(new_path, "/");

        strcat(new_path, path);
    }

    fat16_normalize_path(new_path);

    strcpy(current_path, new_path);

    return 1;
}

int fat16_ls_path(const char *path)
{
    if (!path || path[0] == '\0')
    {
        fat16_ls();
        return 1;
    }

    // resolve cluster if path is directory
    uint16_t cluster;
    if (fat16_resolve_dir_cluster(path, &cluster))
    {
        uint16_t saved_cluster = current_dir_cluster;
        current_dir_cluster = cluster;

        fat16_ls();

        current_dir_cluster = saved_cluster;
        return 1;
    }

    // if not directory, try file entry
    fat16_dir_entry_t entry;
    if (!fat16_find_entry(current_dir_cluster, path, &entry))
        return 0;

    char filename[13];
    fat16_entry_to_name(&entry, filename);

    print("\n");

    if (entry.attr & 0x10)
    {
        print("DIR   ");
        print(filename);
        print("\n");
    }
    else
    {
        print("FILE  ");
        print(filename);
        print(" ");
        print_uint(entry.file_size);
        print(" bytes\n");
    }

    return 1;
}

int fat16_cp_path(const char *srcpath, const char *dstpath)
{
    if (!srcpath || srcpath[0] == '\0')
        return 0;

    if (!dstpath || dstpath[0] == '\0')
        return 0;

    // ---------------- SRC RESOLVE ----------------
    char src_parent[128];
    char src_name[64];

    if (!fat16_split_path(srcpath, src_parent, src_name))
        return 0;

    uint16_t src_parent_cluster;
    if (!fat16_get_cluster_from_path(src_parent, &src_parent_cluster))
        return 0;

    fat16_dir_entry_t src_entry;
    if (!fat16_find_entry(src_parent_cluster, src_name, &src_entry))
        return 0;

    if (src_entry.attr & 0x10)
        return -1; // cannot copy directory

    // ---------------- DST RESOLVE ----------------
    char dst_parent[128];
    char dst_name[64];

    if (!fat16_split_path(dstpath, dst_parent, dst_name))
        return 0;

    uint16_t dst_parent_cluster;
    if (!fat16_get_cluster_from_path(dst_parent, &dst_parent_cluster))
        return 0;

    // if dstpath is actually a directory -> copy inside it
    if (fat16_is_directory_path(dstpath))
    {
        dst_parent_cluster = 0;
        fat16_resolve_dir_cluster(dstpath, &dst_parent_cluster);
        strcpy(dst_name, src_name);
    }

    // destination must not exist
    fat16_dir_entry_t check;
    if (fat16_find_entry(dst_parent_cluster, dst_name, &check))
        return -2;

    // empty file copy
    if (src_entry.file_size == 0 || src_entry.first_cluster_low == 0)
    {
        uint16_t saved = current_dir_cluster;
        current_dir_cluster = dst_parent_cluster;

        int r = fat16_touch(dst_name);

        current_dir_cluster = saved;
        return r;
    }

    uint32_t size = src_entry.file_size;

    uint8_t *buf = (uint8_t *)kmalloc(size);
    if (!buf)
        return 0;

    // read file data
    uint32_t remaining = size;
    uint32_t offset = 0;
    uint16_t cluster = src_entry.first_cluster_low;

    uint8_t sector[512];

    while (cluster < 0xFFF8 && remaining > 0)
    {
        uint32_t start_sector = fat16_cluster_to_sector(cluster);

        for (int s = 0; s < bpb.sectors_per_cluster; s++)
        {
            ata_read_sector(start_sector + s, sector);

            for (int i = 0; i < 512 && remaining > 0; i++)
            {
                buf[offset++] = sector[i];
                remaining--;
            }

            if (remaining == 0)
                break;
        }

        cluster = fat16_get_fat_entry(cluster);
    }

    // write into destination directory
    uint16_t saved = current_dir_cluster;
    current_dir_cluster = dst_parent_cluster;

    int result = fat16_write(dst_name, buf, size);

    current_dir_cluster = saved;
    return result;
}

int fat16_mv_path(const char *srcpath, const char *dstpath)
{
    if (!srcpath || srcpath[0] == '\0')
        return 0;

    if (!dstpath || dstpath[0] == '\0')
        return 0;

    // resolve source entry
    char src_parent[128];
    char src_name[64];

    if (!fat16_split_path(srcpath, src_parent, src_name))
        return 0;

    uint16_t src_parent_cluster;
    if (!fat16_get_cluster_from_path(src_parent, &src_parent_cluster))
        return 0;

    fat16_dir_entry_t src_entry;
    if (!fat16_find_entry(src_parent_cluster, src_name, &src_entry))
        return 0;

    // resolve destination
    char dst_parent[128];
    char dst_name[64];

    if (!fat16_split_path(dstpath, dst_parent, dst_name))
        return 0;

    uint16_t dst_parent_cluster;
    if (!fat16_get_cluster_from_path(dst_parent, &dst_parent_cluster))
        return 0;

    // if dstpath is directory -> move inside directory
    if (fat16_is_directory_path(dstpath))
    {
        fat16_resolve_dir_cluster(dstpath, &dst_parent_cluster);
        strcpy(dst_name, src_name);
    }

    // if same directory: rename only
    if (src_parent_cluster == dst_parent_cluster)
    {
        uint16_t saved = current_dir_cluster;
        current_dir_cluster = src_parent_cluster;

        int r = fat16_mv(src_name, dst_name);

        current_dir_cluster = saved;
        return r;
    }

    // otherwise: copy then delete original
    int c = fat16_cp_path(srcpath, dstpath);
    if (c <= 0)
        return c;

    // delete original entry
    uint16_t saved = current_dir_cluster;
    current_dir_cluster = src_parent_cluster;

    int r;
    if (src_entry.attr & 0x10)
        r = fat16_rmdir(src_name);
    else
        r = fat16_rm(src_name);

    current_dir_cluster = saved;

    return (r > 0) ? 1 : 0;
}
