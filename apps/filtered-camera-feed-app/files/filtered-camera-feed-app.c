#include "dma-api.h"
#include "helper.h"

#include <inttypes.h>

int main(int argc, char *argv[])
{
    const char *udmabuf1_dev = "/dev/udmabuf1";
    const char *uio_dev = "/dev/uio4";

    FILE *input_file_handle;

    uint64_t phy_dest_addr;
    uint32_t size_dest_buf;
    int fd_buf1;
    uint8_t *dest_buf;
    volatile uint8_t *reg_map;

    size_t network_trigger_counter = 0;

    bool finished_operation = false;
    size_t frame_index = 0;
    uint64_t frames_received = 0;

    if (argc > 2)
    {
        printf("Invalid use. Function expects: filtered-camera-feed [visualizer PID]\n");
        exit(1);
    }

    pid_t pid = -1; // means "not provided"

    for (int i = 1; i < argc; i++)
    {
        // otherwise treat it as PID
        char *end = NULL;
        long v = strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || v <= 0)
        {
            fprintf(stderr, "Invalid arg: %s (expected PID)\n", argv[i]);
            return 1;
        }
        pid = (pid_t)v;
    }

    getPhyAddr(DEST_BUF_ID, &phy_dest_addr);
    getBufSize(DEST_BUF_ID, &size_dest_buf);

    printf("Physical address is:\nDestination: 0x%016x\n", phy_dest_addr);
    printf("Buffer sizes are:\n Destination: %d B\n", size_dest_buf);

    if (&phy_dest_addr == NULL)
    {
        fprintf(stderr, "Failed to get a valid address.\n");
        exit(1);
    }

    fd_buf1 = open(udmabuf1_dev, O_RDWR);
    if (fd_buf1 < 0)
    {
        fprintf(stderr, "Failed to open %s: %s\n", udmabuf1_dev, strerror(errno));
        exit(1);
    }

    dest_buf = (uint8_t *)mmap(NULL, (size_t)size_dest_buf, PROT_READ | PROT_WRITE, MAP_SHARED, fd_buf1, 0);
    if (dest_buf == MAP_FAILED)
    {
        perror("mmap(dst)");
        close(fd_buf1);
        return 1;
    }

    int fd_uio = open(uio_dev, O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd_uio < 0)
    {
        perror("open(/dev/uio4)");
        munmap(dest_buf, (size_t)size_dest_buf);
        close(fd_buf1);
        return 1;
    }

    reg_map = (volatile uint8_t *)mmap(NULL, REG_MAP_SIZE,
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED, fd_uio, 0);
    if (reg_map == (void *)MAP_FAILED)
    {
        perror("mmap(regs)");
        close(fd_uio);
        munmap(dest_buf, (size_t)size_dest_buf);
        close(fd_buf1);
        return 1;
    }

    // Prepare DMAs
    // Reset DMA channels
    resetDmaChannel(reg_map, DEST_BUF_ID);
    // Give it a moment
    sleep_ms(10);
    // Start receive channel not to fill the buffer
    startDmaChannel(reg_map, DEST_BUF_ID);
    // Write destination address
    setDmaChannelAddress(reg_map, DEST_BUF_ID, phy_dest_addr);
    // Trigger receive DMA
    printf("Receive DMA channel triggered\n");
    setDmaTransmissionLength(reg_map, DEST_BUF_ID, BYTES_PER_RECEIVE_TRANSMISSION);

    while (!finished_operation)
    {
        // Poll DMA channels
        if (waitDmaTransmissionDone(reg_map, DEST_BUF_ID, 10) == DMA_RECEIVED)
        {
            // Update destination address
            frame_index++;
            frame_index %= 8;
            frames_received++;
            printf("Receive DMA channel finished, frame index, total frames: %u, %" PRIu64 "\n", frame_index, frames_received);

            if (pid > 0)
            {
                if (kill(pid, SIGUSR1) != 0)
                {
                    perror("kill");
                    return 1;
                }
            }

            // Enough frames for one forwarding
            if (frame_index == 0)
            {
                network_trigger_counter++;
            }
            // Update destination address
            setDmaChannelAddress(reg_map, DEST_BUF_ID, (phy_dest_addr + frame_index * BYTES_PER_RECEIVE_TRANSMISSION));
            setDmaTransmissionLength(reg_map, DEST_BUF_ID, BYTES_PER_RECEIVE_TRANSMISSION);
        }
    }

    // Trigger DMA channels
    // Wait for finished transaction

    //  Close on exit
    fclose(input_file_handle);
    munmap((void *)reg_map, REG_MAP_SIZE);
    close(fd_uio);
    munmap(dest_buf, (size_t)size_dest_buf);
    close(fd_buf1);
    return 0;
}