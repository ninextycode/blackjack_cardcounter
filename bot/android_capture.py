import cv2
import image_utils 
from datetime import datetime


class ScreenCapture:
    def __init__(self, device="/dev/video2", save_folder=None):
        self.cap = cv2.VideoCapture(device, cv2.CAP_V4L2)
        if not self.cap.isOpened():
            raise RuntimeError(f"Could not open {device}")
        # Reduce latency:
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.save_folder = save_folder

    def get_screen(self):
        if not self.cap.isOpened():
            raise RuntimeError(f"Capture device is not opened")

        ok, frame = self.cap.read()  # frame is np.ndarray (H, W, 3), dtype=uint8
        if not ok or frame is None:
            raise RuntimeError("Failed to read frame from v4l2 device")
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        
        if self.save_folder is not None:
            dt_tag = datetime.now().strftime("%Y%m%d_%H%M%S")
            filename = f"{self.save_folder}/frame_{dt_tag}.png"
            image_utils.save_rgb_png(frame, filename)        
        return frame

    def close(self):
        self.cap.release()

    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def __del__(self):
        self.close()
