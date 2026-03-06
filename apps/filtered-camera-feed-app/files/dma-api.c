#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdbool.h>

#include "dma-api.h"
#include "helper.h"

// Private helper functions
static inline void reg_write32(volatile const uint8_t *regs, uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(regs + off) = val;
}

static inline uint32_t reg_read32(volatile uint8_t *regs, uint32_t off)
{
    return *(volatile uint32_t *)(regs + off);
}

static int read_file_u64(const char *path, uint64_t *out)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }

    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);

    if (n <= 0)
    {
        fprintf(stderr, "Failed to read %s: %s\n", path, (n == 0) ? "EOF" : strerror(errno));
        return -1;
    }
    buf[n] = '\0';

    errno = 0;
    char *end = NULL;

    // base 0 lets it accept "0x..." hex or decimal automatically
    unsigned long long v = strtoull(buf, &end, 0);

    if (errno != 0)
    {
        fprintf(stderr, "Parse error in %s: %s (buf='%s')\n", path, strerror(errno), buf);
        return -1;
    }
    if (end == buf)
    {
        fprintf(stderr, "Parse error in %s: no number found (buf='%s')\n", path, buf);
        return -1;
    }

    *out = (uint64_t)v;
    return 0;
}

static int read_file_u32(const char *path, uint32_t *content, const char *format)
{
    int fptr;
    char buf[1024];

    fptr = open(path, O_RDONLY);
    if (fptr < 0)
    {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        exit(1);
    }

    read(fptr, buf, 1024);
    sscanf(buf, format, content);
    close(fptr);

    return 0;
}

// Public methods
int DmaInit();

int getPhyAddr(size_t buffer_index, uint64_t *phy_src_addr)
{
    char path[128];

    snprintf(path, sizeof(path), "/sys/class/u-dma-buf/udmabuf%d/phys_addr", buffer_index);
    read_file_u64(path, phy_src_addr);
    return (phy_src_addr == NULL) ? -1 : 0;
}

int getBufSize(size_t buffer_index, uint32_t *size_src_buf)
{
    char path[128];

    snprintf(path, sizeof(path), "/sys/class/u-dma-buf/udmabuf%d/size", buffer_index);
    read_file_u32(path, size_src_buf, "%d");
    return (size_src_buf == NULL) ? -1 : 0;
}

void resetDmaChannel(volatile uint8_t const *reg_map, size_t buffer_index)
{
    uint32_t offset;
    offset = (buffer_index == SRC_BUF_ID) ? MM2S_CRTL : S2MM_CRTL;
    reg_write32(reg_map, offset, DMA_CRTL_RESET);
}

void startDmaChannel(volatile uint8_t const *reg_map, size_t buffer_index)
{
    uint32_t offset;
    offset = (buffer_index == SRC_BUF_ID) ? MM2S_CRTL : S2MM_CRTL;
    reg_write32(reg_map, offset, DMA_CRTL_RUN_STOP | DMA_CTRL_EN_IRQ);
}

void setDmaChannelAddress(volatile uint8_t const *reg_map, size_t buffer_index, uint64_t phy_address)
{
    uint32_t offset, address_lsb, address_msb;
    address_lsb = (uint32_t)(phy_address & 0xFFFFFFFF);
    address_msb = (uint32_t)(phy_address >> 32);

    offset = (buffer_index == SRC_BUF_ID) ? MM2S_SRC_ADDR : S2MM_DEST_ADDR;
    reg_write32(reg_map, offset, address_lsb);
    offset = (buffer_index == SRC_BUF_ID) ? MM2S_SRC_ADDR_MSB : S2MM_DEST_ADDR_MSB;
    reg_write32(reg_map, offset, address_msb);
}

void setDmaTransmissionLength(volatile uint8_t const *reg_map, size_t buffer_index, uint32_t transmission_bytes)
{
    uint32_t offset;
    offset = (buffer_index == SRC_BUF_ID) ? MM2S_LENGTH : S2MM_LENGTH;
    reg_write32(reg_map, offset, transmission_bytes);
}

int waitDmaTransmissionDone(volatile uint8_t *regs, size_t buffer_index, uint8_t timeout_ms)
{
    uint32_t sr_off = (buffer_index == SRC_BUF_ID) ? MM2S_STATUS : S2MM_STATUS;
    for (;;)
    {
        uint32_t sr = reg_read32(regs, sr_off);

        if (sr & DMA_STATUS_IDLE)
        {
            return DMA_RECEIVED;
        }
        if (sr & DMA_STATUS_ERR_IRQ)
        {
            fprintf(stderr, "DMA error: status @ 0x%08x = 0x%08x\n", sr_off, sr);
            return DMA_FAILED;
        }
        if (timeout_ms == 0)
        {
            // fprintf(stderr, "Timeout waiting for DMA idle: status @ 0x%08x = 0x%08x\n", sr_off, sr);
            return DMA_TIMEOUT;
        }
        // ~1ms poll interval
        timeout_ms--;
        sleep_ms(1);
    }
}
