import cv2


def fourcc_to_str(value):
    value = int(value)
    return "".join(chr((value >> 8 * i) & 0xFF) for i in range(4))


requests = [
    (3840, 2160, 30),
    (1920, 1080, 30),
    (1280, 720, 30),
]

for index in range(6):
    print(f"=== camera index {index} ===")
    opened_any = False
    for width, height, fps in requests:
        cap = cv2.VideoCapture(index, cv2.CAP_DSHOW)
        if not cap.isOpened():
            print(f"request {width}x{height}@{fps}: open failed")
            cap.release()
            continue

        opened_any = True
        cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))
        cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        cap.set(cv2.CAP_PROP_FPS, fps)

        ok, frame = cap.read()
        actual_width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        actual_height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        actual_fps = cap.get(cv2.CAP_PROP_FPS)
        actual_fourcc = fourcc_to_str(cap.get(cv2.CAP_PROP_FOURCC))
        shape = None if not ok else f"{frame.shape[1]}x{frame.shape[0]}"
        print(
            f"request {width}x{height}@{fps}: "
            f"opened, read={ok}, prop={actual_width}x{actual_height}@{actual_fps:.2f}, "
            f"fourcc={actual_fourcc!r}, frame={shape}"
        )
        cap.release()

    if not opened_any:
        print("no DirectShow camera opened")
