// Simple mode register map (Xilinx AXI DMA)
#define MM2S_CRTL 0x00         // MM2S DMA Control
#define MM2S_STATUS 0x04       // MM2S DMA Status
#define MM2S_SRC_ADDR 0x18     // MM2S Source Address (low 32)
#define MM2S_SRC_ADDR_MSB 0x1C // MM2S Source Address (high 32) - on 64-bit addr systems
#define MM2S_LENGTH 0x28       // MM2S Transfer Length

#define S2MM_CRTL 0x30          // S2MM DMA Control
#define S2MM_STATUS 0x34        // S2MM DMA Status
#define S2MM_DEST_ADDR 0x48     // S2MM Dest Address (low 32)
#define S2MM_DEST_ADDR_MSB 0x4C // S2MM Dest Address (high 32)
#define S2MM_LENGTH 0x58        // S2MM Transfer Length

// Control bits
#define DMA_CRTL_RUN_STOP (1 << 0) // Run/Stop
#define DMA_CRTL_RESET (1 << 2)    // Reset
#define DMA_CTRL_EN_IRQ (0x7000)

// Status bits (common ones)
#define DMA_STATUS_HALTED (1 << 0)
#define DMA_STATUS_IDLE (1 << 1)
#define DMA_STATUS_ERR_IRQ (1 << 14) // not exhaustive; used for quick sanity

// AXI DMA device-tree node
#define REG_MAP_SIZE 0x10000