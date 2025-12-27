import cv2
import queue
import threading
from android_bot import image_utils 
from datetime import datetime


class ScreenCapture:
    def __init__(self, device="/dev/video2", save_folder=None):
        self.cap = cv2.VideoCapture(device, cv2.CAP_V4L2)
        if not self.cap.isOpened():
            raise RuntimeError(f"Could not open {device}")
        # Reduce latency:
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
        self.save_folder = save_folder
        self._save_queue = queue.Queue()
        self._save_thread = None
        if self.save_folder is not None:
            self._save_thread = threading.Thread(target=self._save_loop, daemon=True)
            self._save_thread.start()

    def get_screen(self):
        if not self.cap.isOpened():
            raise RuntimeError(f"Capture device is not opened")

        ok, frame = self.cap.read()  # frame is np.ndarray (H, W, 3), dtype=uint8
        if not ok or frame is None:
            raise RuntimeError("Failed to read frame from v4l2 device")
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        
        if self._save_thread is not None:
            self._save_queue.put((frame, datetime.now()))
        return frame

    def _save_loop(self):
        while True:
            item = self._save_queue.get()
            if item is None:
                break
            frame, timestamp = item
            dt_tag = timestamp.strftime("%Y%m%d_%H%M%S_%f")
            filename = f"{self.save_folder}/frame_{dt_tag}.png"
            image_utils.save_rgb_png(frame, filename)

    def close(self):
        if self._save_thread is not None:
            self._save_queue.put(None)
            self._save_thread.join()
            self._save_thread = None
        self.cap.release()

    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_value, traceback):
        self.close()

    def __del__(self):
        self.close()
