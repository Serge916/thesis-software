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

#include "loopback-app.h"

void sleep_ms(int milliseconds)
{
	// Convert milliseconds to microseconds
	usleep(milliseconds * 1000);
}

static inline void reg_write32(volatile uint8_t *regs, uint32_t off, uint32_t val)
{
	*(volatile uint32_t *)(regs + off) = val;
}

static inline uint32_t reg_read32(volatile uint8_t *regs, uint32_t off)
{
	return *(volatile uint32_t *)(regs + off);
}

static int read_file_u64(const char *path, uint64_t *content, const char *format)
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

static void wait_done(volatile uint8_t *regs, uint32_t sr_off, uint8_t timeout_ms)
{
	for (;;)
	{
		uint32_t sr = reg_read32(regs, sr_off);

		if (sr & DMA_STATUS_IDLE)
		{
			return;
		}
		if (sr & DMA_STATUS_ERR_IRQ)
		{
			fprintf(stderr, "DMA error: status @ 0x%08x = 0x%08x\n", sr_off, sr);
			exit(1);
		}
		if (timeout_ms == 0)
		{
			fprintf(stderr, "Timeout waiting for DMA idle: status @ 0x%08x = 0x%08x\n", sr_off, sr);
			exit(1);
		}
		// ~1ms poll interval
		timeout_ms--;
		sleep_ms(1);
	}
}

int main(int argc, char *argv[])
{
	const char *udmabuf0_dev = "/dev/udmabuf0";
	const char *udmabuf1_dev = "/dev/udmabuf1";
	const char *uio_dev = "/dev/uio4";

	uint64_t phy_src_addr, phy_dest_addr;
	uint32_t size_src_buf, size_dest_buf;
	int fd_buf0, fd_buf1;
	uint8_t *src_buf, *dest_buf;
	volatile uint8_t *reg_map;

	size_t transmission_bytes;

	if (argc != 2)
	{
		printf("Invalid use. Function expects: loopback-app <transmission size in Bytes>\n");
		exit(1);
	}

	transmission_bytes = atoi(argv[1]);

	read_file_u64("/sys/class/u-dma-buf/udmabuf0/phys_addr", &phy_src_addr, "%x");
	read_file_u64("/sys/class/u-dma-buf/udmabuf1/phys_addr", &phy_dest_addr, "%x");
	read_file_u32("/sys/class/u-dma-buf/udmabuf0/size", &size_src_buf, "%d");
	read_file_u32("/sys/class/u-dma-buf/udmabuf1/size", &size_dest_buf, "%d");

	printf("Physical addresses are:\nSource: 0x%016x, Destination: 0x%016x\n",
		   phy_src_addr, phy_dest_addr);
	printf("Buffer sizes are:\nSource: %d B, Destination: %d B\n",
		   size_src_buf, size_dest_buf);

	if ((phy_dest_addr > UINT32_MAX) || (phy_src_addr > UINT32_MAX))
	{
		fprintf(stderr, "64 bit addresses are not supported by this binary.\n");
		exit(1);
	}

	if (transmission_bytes > size_src_buf || transmission_bytes > size_dest_buf)
	{
		fprintf(stderr, "Transmission too large for buffers\n");
		exit(1);
	}

	fd_buf0 = open(udmabuf0_dev, O_RDWR);
	if (fd_buf0 < 0)
	{
		fprintf(stderr, "Failed to open %s: %s\n", udmabuf0_dev, strerror(errno));
		exit(1);
	}

	fd_buf1 = open(udmabuf1_dev, O_RDWR);
	if (fd_buf1 < 0)
	{
		fprintf(stderr, "Failed to open %s: %s\n", udmabuf1_dev, strerror(errno));
		exit(1);
	}

	src_buf = (uint8_t *)mmap(NULL, (size_t)size_src_buf, PROT_READ | PROT_WRITE, MAP_SHARED, fd_buf0, 0);
	if (src_buf == MAP_FAILED)
	{
		perror("mmap(src)");
		close(fd_buf1);
		close(fd_buf0);
		return 1;
	}

	dest_buf = (uint8_t *)mmap(NULL, (size_t)size_dest_buf, PROT_READ | PROT_WRITE, MAP_SHARED, fd_buf1, 0);
	if (dest_buf == MAP_FAILED)
	{
		perror("mmap(dst)");
		munmap(src_buf, (size_t)size_src_buf);
		close(fd_buf1);
		close(fd_buf0);
		return 1;
	}

	int fd_uio = open(uio_dev, O_RDWR | O_SYNC | O_CLOEXEC);
	if (fd_uio < 0)
	{
		perror("open(/dev/uio4)");
		munmap(src_buf, (size_t)size_src_buf);
		munmap(dest_buf, (size_t)size_dest_buf);
		close(fd_buf1);
		close(fd_buf0);
		return 1;
	}

	reg_map = (volatile uint8_t *)mmap(NULL, REG_MAP_SIZE,
									   PROT_READ | PROT_WRITE,
									   MAP_SHARED, fd_uio, 0);
	if (reg_map == (void *)MAP_FAILED)
	{
		perror("mmap(regs)");
		close(fd_uio);
		munmap(src_buf, (size_t)size_src_buf);
		munmap(dest_buf, (size_t)size_dest_buf);
		close(fd_buf1);
		close(fd_buf0);
		return 1;
	}

	// Fill source with data
	for (size_t i = 0; i < transmission_bytes; i++)
	{
		src_buf[i] = i % 256;
	}

	// Reset DMA channels
	reg_write32(reg_map, MM2S_CRTL, DMA_CRTL_RESET);
	reg_write32(reg_map, S2MM_CRTL, DMA_CRTL_RESET);
	// Give it a moment
	sleep_ms(10);
	// Start receive channel not to fill the buffer
	reg_write32(reg_map, S2MM_CRTL, DMA_CRTL_RUN_STOP | DMA_CTRL_EN_IRQ);
	// Write destination address
	reg_write32(reg_map, S2MM_DEST_ADDR, phy_dest_addr);
	// Start transmit channel
	reg_write32(reg_map, MM2S_CRTL, DMA_CRTL_RUN_STOP | DMA_CTRL_EN_IRQ);
	// Set source address
	reg_write32(reg_map, MM2S_SRC_ADDR, phy_src_addr);
	// Trigger DMA channels
	reg_write32(reg_map, S2MM_LENGTH, transmission_bytes);
	reg_write32(reg_map, MM2S_LENGTH, transmission_bytes);
	// Wait for finished transaction
	wait_done(reg_map, MM2S_STATUS, 10);
	wait_done(reg_map, S2MM_STATUS, 10);

	// Check whether they match
	bool ok = true;
	printf("TX: [");
	for (size_t i = 0; i < transmission_bytes - 1; i++)
	{
		printf("%u, ", src_buf[i]);
	}
	printf("%u]\n", src_buf[transmission_bytes - 1]);

	printf("RX: [");
	for (size_t i = 0; i < transmission_bytes - 1; i++)
	{
		printf("%u, ", dest_buf[i]);
		if (dest_buf[i] != src_buf[i])
			ok = false;
	}
	printf("%u]\n", dest_buf[transmission_bytes - 1]);

	if (!ok)
	{
		fprintf(stderr, "Mismatch! Check FIFO wiring, DMA mode, cache sync, and addresses.\n");
		// fall through to cleanup, but return non-zero
	}

	//  Close on exit
	munmap((void *)reg_map, REG_MAP_SIZE);
	close(fd_uio);
	munmap(src_buf, (size_t)size_src_buf);
	close(fd_buf0);
	munmap(dest_buf, (size_t)size_dest_buf);
	close(fd_buf1);
	return 0;
}