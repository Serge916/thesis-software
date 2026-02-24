#include "dma-api.h"
#include "helper.h"

#include <inttypes.h>

int main(int argc, char *argv[])
{
    const char *udmabuf0_dev = "/dev/udmabuf0";
    const char *udmabuf1_dev = "/dev/udmabuf1";
    const char *uio_dev = "/dev/uio4";

    FILE *input_file_handle;

    uint64_t phy_src_addr, phy_dest_addr;
    uint32_t size_src_buf, size_dest_buf;
    int fd_buf0, fd_buf1;
    uint8_t *src_buf, *dest_buf;
    volatile uint8_t *reg_map;

    // 2048B is 128 lines, i.e. 2048/16.
    char line_buf[17];
    // 128 lines is 1024B, i.e. 128*8
    size_t transmission_bytes = 1024;
    size_t network_trigger_counter = 0;

    uint64_t lineno = 0;
    bool finished_operation = false;
    bool finished_transmitting = false;
    bool transmit_slot_available = true;
    size_t frame_index = 0;
    uint64_t frames_received = 0;

    if (argc < 2 || argc > 4)
    {
        printf("Invalid use. Function expects: stream-from-file <path to input file> [visualizer PID] [--loop]\n");
        exit(1);
    }

    pid_t pid = -1; // means "not provided"
    bool loop_file = false;

    for (int i = 2; i < argc; i++)
    {
        if (strcmp(argv[i], "--loop") == 0)
        {
            loop_file = true;
            continue;
        }

        // otherwise treat it as PID
        char *end = NULL;
        long v = strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || v <= 0)
        {
            fprintf(stderr, "Invalid arg: %s (expected PID or --loop)\n", argv[i]);
            return 1;
        }
        pid = (pid_t)v;
    }

    input_file_handle = fopen(argv[1], "r");
    if (!input_file_handle)
    {
        fprintf(stderr, "fopen: %s\n", strerror(errno));
        printf("Provide a valid value, i.e. path to input file\n");
        exit(1);
    }

    getPhyAddr(SRC_BUF_ID, &phy_src_addr);
    getPhyAddr(DEST_BUF_ID, &phy_dest_addr);
    getBufSize(SRC_BUF_ID, &size_src_buf);
    getBufSize(DEST_BUF_ID, &size_dest_buf);

    printf("Physical addresses are:\nSource: 0x%016x, Destination: 0x%016x\n",
           phy_src_addr, phy_dest_addr);
    printf("Buffer sizes are:\nSource: %d B, Destination: %d B\n",
           size_src_buf, size_dest_buf);

    if ((&phy_src_addr == NULL) || (&phy_dest_addr == NULL))
    {
        fprintf(stderr, "Failed to get a valid address.\n");
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

    // Prepare DMAs
    // Reset DMA channels
    resetDmaChannel(reg_map, SRC_BUF_ID);
    resetDmaChannel(reg_map, DEST_BUF_ID);
    // Give it a moment
    sleep_ms(10);
    // Start receive channel not to fill the buffer
    startDmaChannel(reg_map, DEST_BUF_ID);
    // Start transmit channel
    startDmaChannel(reg_map, SRC_BUF_ID);
    // Write destination address
    setDmaChannelAddress(reg_map, DEST_BUF_ID, phy_dest_addr);
    // Set source address
    setDmaChannelAddress(reg_map, SRC_BUF_ID, phy_src_addr);
    // Trigger receive DMA
    printf("Receive DMA channel triggered\n");
    setDmaTransmissionLength(reg_map, DEST_BUF_ID, BYTES_PER_RECEIVE_TRANSMISSION);

    while (!finished_operation)
    {
        size_t lines_read = 0;
        uint8_t bytes[BYTES_PER_LINE];

        // Fill a chunk with up to 128 parsed lines
        while (lines_read < LINES_PER_CHUNK && !finished_transmitting && transmit_slot_available)
        {
            int r = readNextLine(input_file_handle, line_buf, &lineno);
            if (r == 0)
            {
                if (loop_file)
                {
                    printf("INFO: Reached EOF, rewinding input and continuing\n");
                    clearerr(input_file_handle);
                    rewind(input_file_handle);
                    lineno = 0; // optional
                    continue;   // keep filling this chunk
                }
                else
                {
                    finished_transmitting = true;
                    printf("INFO: Finished sending events\n");
                    break; // original behavior
                }
            }

            if (r < 0)
            { // parse error
                fprintf(stderr, "Parse error. result < 0, lineno: %d\n", lineno);
                finished_transmitting = true;
                break;
            }
            // printf("DEBUG: Read line %d, %s\n", lineno, line_buf);
            if (parseLine(line_buf, bytes) != 0)
            {
                fprintf(stderr, "Line %d: invalid hex digit\n", lineno);
                finished_transmitting = true;
                break;
            }
            // printf("DEBUG: Line %d parsed,", lineno);
            // for (size_t i = 0; i < BYTES_PER_LINE - 1; i++)
            // {
            //     printf(" %02x", bytes[i]);
            // }
            // printf(" %02x\n", bytes[BYTES_PER_LINE - 1]);

            memcpy(&src_buf[lines_read * BYTES_PER_LINE], bytes, BYTES_PER_LINE);
            lines_read++;
        }
        // Copy into the DMA source buffer
        if (lines_read > 0)
        {
            // printf("DEBUG: Line %d copied\n", lineno);
            transmit_slot_available = false;
            setDmaTransmissionLength(reg_map, SRC_BUF_ID, lines_read * BYTES_PER_LINE);
            // printf("DEBUG: Transmit DMA channel triggered\n");
        }

        // if (lines_read == 0 && !finished_transmitting)
        // {
        //     // No more data (EOF reached before any new lines for this chunk)
        //     finished_transmitting = true;
        //     break;
        // }

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

        if (!transmit_slot_available)
        {
            if (waitDmaTransmissionDone(reg_map, SRC_BUF_ID, 10) == DMA_RECEIVED)
            {
                // One more transmission
                // printf("DEBUG: Transmit DMA channel finished\n");
                transmit_slot_available = true;
            }
        }
    }

    // Trigger DMA channels
    // Wait for finished transaction

    //  Close on exit
    fclose(input_file_handle);
    munmap((void *)reg_map, REG_MAP_SIZE);
    close(fd_uio);
    munmap(src_buf, (size_t)size_src_buf);
    close(fd_buf0);
    munmap(dest_buf, (size_t)size_dest_buf);
    close(fd_buf1);
    return 0;
}