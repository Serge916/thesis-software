#!/usr/bin/env python3
import os
import mmap
import struct
import time


# ---------------------------
# Helpers: sysfs reads
# ---------------------------
def read_int(path, base=10):
    with open(path, "r") as f:
        s = f.read().strip()
    return int(s, base)


def read_hex(path):
    return read_int(path, 16)


# ---------------------------
# AXI DMA register offsets
# ---------------------------
# Simple mode register map (Xilinx AXI DMA)
MM2S_DMACR = 0x00  # MM2S DMA Control
MM2S_DMASR = 0x04  # MM2S DMA Status
MM2S_SA = 0x18  # MM2S Source Address (low 32)
MM2S_SA_MSB = 0x1C  # MM2S Source Address (high 32) - on 64-bit addr systems
MM2S_LENGTH = 0x28  # MM2S Transfer Length

S2MM_DMACR = 0x30  # S2MM DMA Control
S2MM_DMASR = 0x34  # S2MM DMA Status
S2MM_DA = 0x48  # S2MM Dest Address (low 32)
S2MM_DA_MSB = 0x4C  # S2MM Dest Address (high 32)
S2MM_LENGTH = 0x58  # S2MM Transfer Length

# Control bits
DMACR_RS = 1 << 0  # Run/Stop
DMACR_RESET = 1 << 2  # Reset

# Status bits (common ones)
DMASR_HALTED = 1 << 0
DMASR_IDLE = 1 << 1
DMASR_ERR_IRQ = 1 << 14  # not exhaustive; used for quick sanity


def reg_read32(mm, off):
    return struct.unpack_from("<I", mm, off)[0]


def reg_write32(mm, off, val):
    struct.pack_into("<I", mm, off, val & 0xFFFFFFFF)


def write_addr64(mm, lo_off, hi_off, addr):
    lo = addr & 0xFFFFFFFF
    hi = (addr >> 32) & 0xFFFFFFFF
    reg_write32(mm, lo_off, lo)
    reg_write32(mm, hi_off, hi)


def dma_reset(mm):
    # Reset both channels
    reg_write32(mm, MM2S_DMACR, DMACR_RESET)
    reg_write32(mm, S2MM_DMACR, DMACR_RESET)

    # Reset bit self-clears; wait a moment
    time.sleep(0.01)

    # Optional: sanity check
    # After reset, channels typically halted
    # print("MM2S_DMASR:", hex(reg_read32(mm, MM2S_DMASR)))
    # print("S2MM_DMASR:", hex(reg_read32(mm, S2MM_DMASR)))


def wait_done(mm, sr_off, timeout_s=1.0):
    t0 = time.time()
    while True:
        sr = reg_read32(mm, sr_off)
        # In simple mode, "IDLE" is the easiest done indicator
        if (sr & DMASR_IDLE) != 0:
            return sr
        # crude error check
        if (sr & DMASR_ERR_IRQ) != 0:
            raise RuntimeError(f"DMA error (status @ {hex(sr_off)} = {hex(sr)})")
        if (time.time() - t0) > timeout_s:
            raise TimeoutError(
                f"Timeout waiting for DMA idle (status @ {hex(sr_off)} = {hex(sr)})"
            )
        time.sleep(0.001)


def main():
    # ---------------------------
    # 1) Map udmabuf0
    # ---------------------------
    udmabuf0_dev = "/dev/udmabuf0"
    udmabuf1_dev = "/dev/udmabuf1"
    phy_src_addr = read_hex("/sys/class/u-dma-buf/udmabuf0/phys_addr")
    size_src_buf = read_int("/sys/class/u-dma-buf/udmabuf0/size", 10)
    phy_dest_addr = read_hex("/sys/class/u-dma-buf/udmabuf1/phys_addr")
    size_dest_buf = read_int("/sys/class/u-dma-buf/udmabuf1/size", 10)

    fd_buf0 = os.open(udmabuf0_dev, os.O_RDWR)
    fd_buf1 = os.open(udmabuf1_dev, os.O_RDWR)

    src_buf = mmap.mmap(
        fd_buf0, size_src_buf, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE
    )
    dest_buf = mmap.mmap(
        fd_buf1, size_dest_buf, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE
    )

    # offsets inside udmabufs
    SRC_OFF = 0x000
    DST_OFF = 0x000
    VALUE_AMOUNT = 512

    # write little-endian uint32 values
    tx = list(range(VALUE_AMOUNT))
    for i, v in enumerate(tx):
        struct.pack_into("<I", src_buf, SRC_OFF + 4 * i, v)

    # ---------------------------
    # 2) Map AXI DMA regs via UIO
    # ---------------------------
    uio_dev = "/dev/uio4"

    fd_uio = os.open(uio_dev, os.O_RDWR | os.O_SYNC)
    # 0x10000 matches your reg size; if your UIO exposes smaller, adjust.
    regs = mmap.mmap(fd_uio, 0x10000, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE)

    # ---------------------------
    # 3) Program DMA: S2MM first, then MM2S
    # ---------------------------
    dma_reset(regs)

    # Start S2MM (dest) channel
    reg_write32(regs, S2MM_DMACR, DMACR_RS)

    dst_phys = phy_dest_addr
    write_addr64(regs, S2MM_DA, S2MM_DA_MSB, dst_phys)

    # Start MM2S (source) channel
    reg_write32(regs, MM2S_DMACR, DMACR_RS)

    src_phys = phy_src_addr + SRC_OFF
    write_addr64(regs, MM2S_SA, MM2S_SA_MSB, src_phys)

    length_bytes = VALUE_AMOUNT * 4

    # Kick transfers: write LENGTH registers
    # (Writing LENGTH generally starts the actual move)
    reg_write32(regs, S2MM_LENGTH, length_bytes)
    reg_write32(regs, MM2S_LENGTH, length_bytes)

    # Wait for completion
    wait_done(regs, MM2S_DMASR, timeout_s=1.0)
    wait_done(regs, S2MM_DMASR, timeout_s=1.0)

    # ---------------------------
    # 4) Read back from destination
    # ---------------------------
    rx = [
        struct.unpack_from("<I", dest_buf, DST_OFF + 4 * i)[0]
        for i in range(VALUE_AMOUNT)
    ]

    print("TX:", tx)
    print("RX:", rx)

    if rx != tx:
        raise SystemExit(
            "Mismatch! Check FIFO wiring, DMA mode, cache sync, and addresses."
        )

    # cleanup
    regs.close()
    os.close(fd_uio)
    src_buf.close()
    os.close(fd_buf0)
    dest_buf.close()
    os.close(fd_buf1)


if __name__ == "__main__":
    main()
