import cv2
import numpy as np
import pytesseract


pytesseract_config = "--oem 3 --psm 6 -c tessedit_char_whitelist=0123456789JQKA"


def to_bnw(img_rgb):
    """
    Convert an RGB image to binary black/white such that:
    - Red regions become black (0)
    - Blue regions become white (255)
    - Dark/black text also becomes black
    - Remaining background defaults to white
    Returns a 2D uint8 image (0/255).
    """
    hsv = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2HSV)
    
    # Red wraps around in HSV
    red1 = cv2.inRange(hsv, np.array([0, 70, 40]),  np.array([15, 255, 255]))
    red2 = cv2.inRange(hsv, np.array([165, 70, 40]), np.array([180, 255, 255]))
    red_mask = cv2.bitwise_or(red1, red2)
    
    # Dark pixels (black text, outlines, etc.) from grayscale
    gray = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2GRAY)
    
    # Blue range in HSV
    blue_mask = cv2.inRange(hsv, np.array([95, 80, 40]), np.array([140, 255, 255]))
    
    # Start with white background
    bnw = np.full_like(gray, 255)
    
    # Apply mappings
    bnw[red_mask > 0] = 0        # red -> black
    bnw[gray < 128] = 0          # dark -> black
    bnw[blue_mask > 0] = 255     # blue -> white (explicit map)

    return bnw


def ocr_digit(digit):
    digit = to_bnw(digit)
    digit = cv2.resize(digit, (28, 28), interpolation=cv2.INTER_AREA)
    text = pytesseract.image_to_string(digit, config=pytesseract_config).strip()
    if text == "10":
        return "T"
    return text