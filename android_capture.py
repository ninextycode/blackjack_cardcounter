import cv2


class ScreenCapture:
    def __init__(self, device="/dev/video2"):
        self.cap = cv2.VideoCapture(device, cv2.CAP_V4L2)
        if not self.cap.isOpened():
            raise RuntimeError(f"Could not open {device}")
        # Reduce latency:
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)

    def get_screen(self):
        ok, frame = self.cap.read()  # frame is np.ndarray (H, W, 3), dtype=uint8
        if not ok or frame is None:
            raise RuntimeError("Failed to read frame from v4l2 device")
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        return frame

    def close(self):
        self.cap.release()

    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def __del__(self):
        self.close()
