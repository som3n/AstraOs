#include "drivers/ata.h"
#include "drivers/ports.h"

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CTRL 0x3F6

#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_SECCOUNT0 0x02
#define ATA_REG_LBA0 0x03
#define ATA_REG_LBA1 0x04
#define ATA_REG_LBA2 0x05
#define ATA_REG_HDDEVSEL 0x06
#define ATA_REG_COMMAND 0x07
#define ATA_REG_STATUS 0x07

#define ATA_CMD_READ_PIO 0x20

#define ATA_STATUS_BSY 0x80
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_ERR 0x01

static void ata_wait_bsy()
{
    while (inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_STATUS_BSY)
    {
    }
}

static void ata_wait_drq()
{
    while (!(inb(ATA_PRIMARY_IO + ATA_REG_STATUS) & ATA_STATUS_DRQ))
    {
    }
}

void ata_read_sector(uint32_t lba, uint8_t *buffer)
{

    ata_wait_bsy();

    outb(ATA_PRIMARY_IO + ATA_REG_HDDEVSEL, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_IO + ATA_REG_SECCOUNT0, 1);

    outb(ATA_PRIMARY_IO + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_PRIMARY_IO + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));

    outb(ATA_PRIMARY_IO + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    ata_wait_bsy();
    ata_wait_drq();

    for (int i = 0; i < 256; i++)
    {
        uint16_t data = inw(ATA_PRIMARY_IO + ATA_REG_DATA);
        buffer[i * 2] = (uint8_t)(data & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)((data >> 8) & 0xFF);
    }
}

void ata_write_sector(uint32_t lba, uint8_t *buffer) {

    // Wait until not busy
    while (inb(0x1F7) & 0x80);

    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);                 // sector count
    outb(0x1F3, (uint8_t)(lba));     // LBA low
    outb(0x1F4, (uint8_t)(lba >> 8));// LBA mid
    outb(0x1F5, (uint8_t)(lba >> 16));// LBA high
    outb(0x1F7, 0x30);              // WRITE SECTORS command

    // Wait until drive is ready
    while (!(inb(0x1F7) & 0x08));

    // Write 256 words (512 bytes)
    for (int i = 0; i < 256; i++) {
        uint16_t word = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outw(0x1F0, word);
    }

    // Flush cache
    outb(0x1F7, 0xE7);

    // Wait for completion
    while (inb(0x1F7) & 0x80);
}
