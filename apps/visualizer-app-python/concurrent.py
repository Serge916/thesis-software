import os
import mmap
import numpy as np
import cv2
import signal

W, H = 128, 128
CH_BYTES = (W * H) // 8
FRAME_STRIDE = CH_BYTES * 2

divider = 10

divider_bgr = (40, 40, 40)
border_bgr = (120, 120, 120)

win = "udmabuf 8 frames (SIGUSR1 refresh)"
bitorder = "big"

TARGET_W, TARGET_H = 1920, 1080

rows, cols = 2, 4
divider = 10

# Compute the largest integer scale that fits the grid into 1920x1080
scale_w = (TARGET_W - (cols - 1) * divider) // (cols * W)
scale_h = (TARGET_H - (rows - 1) * divider) // (rows * H)
scale = max(1, min(scale_w, scale_h))

tile_w, tile_h = W * scale, H * scale
mosaic_w = cols * tile_w + (cols - 1) * divider
mosaic_h = rows * tile_h + (rows - 1) * divider


REFRESH_SIG = signal.SIGUSR1
_refresh_requested = True  # start with initial draw


def letterbox_to_1080p(img, target_w=1920, target_h=1080, bg=(0, 0, 0)):
    out = np.full((target_h, target_w, 3), bg, dtype=np.uint8)
    h, w = img.shape[:2]
    x0 = (target_w - w) // 2
    y0 = (target_h - h) // 2
    out[y0 : y0 + h, x0 : x0 + w] = img
    return out


def udmabuf_size_from_sysfs(dev_path: str) -> int:
    name = os.path.basename(dev_path)
    sysfs = f"/sys/class/u-dma-buf/{name}/size"
    with open(sysfs, "r", encoding="utf-8") as f:
        return int(f.read().strip())


def decode_frame_bgr(raw4096: bytes, bitorder: str = "big") -> np.ndarray:
    raw0 = raw4096[:CH_BYTES]
    raw1 = raw4096[CH_BYTES : CH_BYTES * 2]

    b0 = np.frombuffer(raw0, dtype=np.uint8)
    b1 = np.frombuffer(raw1, dtype=np.uint8)

    bits0 = np.unpackbits(b0, bitorder=bitorder)[: W * H].reshape(H, W).astype(np.uint8)
    bits1 = np.unpackbits(b1, bitorder=bitorder)[: W * H].reshape(H, W).astype(np.uint8)

    img = np.zeros((H, W, 3), dtype=np.uint8)  # BGR
    img[..., 0] = bits0 * 255  # Blue
    img[..., 2] = bits1 * 255  # Red
    return img


def blit_tile(mm: mmap.mmap, mosaic: np.ndarray, i: int):
    mm.seek(i * FRAME_STRIDE)
    raw = mm.read(FRAME_STRIDE)
    if len(raw) != FRAME_STRIDE:
        return

    frame = decode_frame_bgr(raw, bitorder=bitorder)
    frame_scaled = cv2.resize(frame, (tile_w, tile_h), interpolation=cv2.INTER_NEAREST)

    r, c = divmod(i, cols)
    x0 = c * (tile_w + divider)
    y0 = r * (tile_h + divider)
    x1 = x0 + tile_w
    y1 = y0 + tile_h

    mosaic[y0:y1, x0:x1] = frame_scaled
    cv2.rectangle(mosaic, (x0, y0), (x1 - 1, y1 - 1), border_bgr, 1)

    label = str(i)  # 0 is base address
    org = (x0 + 8, y0 + 24)
    cv2.putText(
        mosaic, label, org, cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 0), 3, cv2.LINE_AA
    )
    cv2.putText(
        mosaic,
        label,
        org,
        cv2.FONT_HERSHEY_SIMPLEX,
        0.8,
        (255, 255, 255),
        1,
        cv2.LINE_AA,
    )


def _request_refresh(_signum, _frame):
    # Coalesces automatically: many SIGUSR1s just keep this True.
    global _refresh_requested
    _refresh_requested = True


def main(dev_path="/dev/udmabuf1"):
    global bitorder, _refresh_requested

    signal.signal(REFRESH_SIG, _request_refresh)
    print(f"Python PID: {os.getpid()}  (send SIGUSR1 to request refresh)")

    size = udmabuf_size_from_sysfs(dev_path)
    n_frames = size // FRAME_STRIDE
    if n_frames < 8:
        raise RuntimeError(f"{dev_path} has {n_frames} frames; need at least 8.")

    fd = os.open(dev_path, os.O_RDONLY | os.O_SYNC)
    try:
        mm = mmap.mmap(fd, size, access=mmap.ACCESS_READ)
        try:
            cv2.namedWindow(win, cv2.WINDOW_NORMAL)
            cv2.resizeWindow(win, 1920, 1080)  # request window size

            mosaic = np.full((mosaic_h, mosaic_w, 3), divider_bgr, dtype=np.uint8)

            while True:
                key = cv2.waitKey(1) & 0xFF
                if key in (27, ord("q")):
                    break
                elif key == ord("b"):
                    bitorder = "big"
                    _refresh_requested = True
                    print("bitorder=big")
                elif key == ord("l"):
                    bitorder = "little"
                    _refresh_requested = True
                    print("bitorder=little")

                if cv2.getWindowProperty(win, cv2.WND_PROP_VISIBLE) == 0:
                    break

                if _refresh_requested:
                    _refresh_requested = False
                    # refresh all 8 tiles
                    for i in range(8):
                        blit_tile(mm, mosaic, i)
                    frame1080 = letterbox_to_1080p(mosaic, 1920, 1080, bg=(0, 0, 0))
                    cv2.imshow(win, frame1080)

        finally:
            mm.close()
    finally:
        os.close(fd)

    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
