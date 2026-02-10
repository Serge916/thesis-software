import os
import mmap
import numpy as np
import cv2

W, H = 128, 128
FRAME_BYTES = (W * H) // 8  # 2048 bytes per 1bpp channel
FRAME_STRIDE = FRAME_BYTES * 2  # 4096 bytes per 2-channel frame


def udmabuf_size_from_sysfs(dev_path: str) -> int:
    """
    For /dev/udmabuf1 -> reads /sys/class/u-dma-buf/udmabuf1/size
    """
    name = os.path.basename(dev_path)  # "udmabuf1"
    sysfs = f"/sys/class/u-dma-buf/{name}/size"
    with open(sysfs, "r", encoding="utf-8") as f:
        return int(f.read().strip())


def decode_frame(raw: bytes, bitorder: str = "big") -> np.ndarray:
    """raw is exactly FRAME_STRIDE bytes: [blue(2048), red(2048)]"""
    raw0 = raw[:FRAME_BYTES]  # channel 0 -> blue
    raw1 = raw[FRAME_BYTES:FRAME_STRIDE]  # channel 1 -> red

    b0 = np.frombuffer(raw0, dtype=np.uint8)
    b1 = np.frombuffer(raw1, dtype=np.uint8)

    bits0 = np.unpackbits(b0, bitorder=bitorder)[: W * H].reshape(H, W).astype(np.uint8)
    bits1 = np.unpackbits(b1, bitorder=bitorder)[: W * H].reshape(H, W).astype(np.uint8)

    img = np.zeros((H, W, 3), dtype=np.uint8)  # BGR for OpenCV
    img[..., 0] = bits0 * 255  # Blue
    img[..., 2] = bits1 * 255  # Red
    return img


def main(dev_path="/dev/udmabuf1"):
    scale = 6
    win = "udmabuf"
    bitorder = "little"

    print(
        "Display the frames in the destination buffer.\nYou can move backwards (n) or forward (m).\nUse (q) to quit.\n"
    )

    size = udmabuf_size_from_sysfs(dev_path)
    if size < FRAME_STRIDE:
        raise RuntimeError(
            f"{dev_path} size is {size} bytes; need at least {FRAME_STRIDE} for one frame."
        )

    n_frames = size // FRAME_STRIDE
    if n_frames == 0:
        raise RuntimeError(
            f"{dev_path} does not contain any complete {FRAME_STRIDE}-byte frames."
        )

    fd = os.open(dev_path, os.O_RDONLY | os.O_SYNC)
    try:
        mm = mmap.mmap(fd, size, access=mmap.ACCESS_READ)
        try:
            frame_idx = 0

            while True:
                # Clamp at boundaries (no wrap)
                if frame_idx < 0:
                    frame_idx = 0
                elif frame_idx > n_frames - 1:
                    frame_idx = n_frames - 1

                offset = frame_idx * FRAME_STRIDE
                mm.seek(offset)
                raw = mm.read(FRAME_STRIDE)
                if len(raw) != FRAME_STRIDE:
                    print("Short read; stopping.")
                    break

                img = decode_frame(raw, bitorder=bitorder)
                vis = cv2.resize(
                    img, (W * scale, H * scale), interpolation=cv2.INTER_NEAREST
                )
                cv2.imshow(win, vis)

                # Step mode: wait for a key
                key = cv2.waitKey(0) & 0xFF

                # If user closed the window
                prop = cv2.getWindowProperty(win, cv2.WND_PROP_VISIBLE)
                if prop == 0:
                    break

                if key == 27 or key == ord("q"):  # ESC or q
                    break
                elif key == ord("b"):
                    bitorder = "big"
                    print("bitorder=big")
                elif key == ord("l"):
                    bitorder = "little"
                    print("bitorder=little")

                # Frame stepping
                elif key == ord("m"):  # forward
                    if frame_idx < n_frames - 1:
                        frame_idx += 1
                    else:
                        print("Already at last frame.")
                elif key == ord("n"):  # backward
                    if frame_idx > 0:
                        frame_idx -= 1
                    else:
                        print("Already at first frame.")

                if key in (ord("m"), ord("n")):
                    print(
                        f"frame {frame_idx + 1}/{n_frames} (offset {frame_idx * FRAME_STRIDE} bytes)"
                    )

        finally:
            mm.close()
    finally:
        os.close(fd)

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
