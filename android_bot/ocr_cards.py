import cv2
import numpy as np
import pytesseract
from android_bot import image_utils
import glob
import os
from android_bot.image_assets import ImageAssets


# psm 7 - single text line
pytesseract_config = "--oem 3 --psm 7 -c tessedit_char_whitelist=0123456789JQKA"


def _load_digits():
    digit_images = {}
    filepaths = glob.glob(str(ImageAssets.root_dir / "digits" / "*.png"))
    for filepath in filepaths:
        # Extract digit/character from filename (e.g., "2" from "digits/2_16x16.png")
        filename = os.path.basename(filepath)
        digit_char = filename.split("_")[0]
        img = image_utils.load_rdb(filepath)
        digit_images[digit_char] = cv2.cvtColor(img, cv2.COLOR_RGB2GRAY)   
    return digit_images

digit_images = _load_digits()



def to_bnw(img_rgb, threshold=128):
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
    red_mask = image_utils.get_red_mask_hsv(hsv)
    
    # Dark pixels (black text, outlines, etc.) from grayscale
    gray = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2GRAY)
    
    # Blue range in HSV
    blue_mask = image_utils.get_blue_mask_hsv(hsv)
    
    # Start with white background
    bnw = np.full_like(gray, 255)
    
    # Apply mappings
    bnw[red_mask] = 0        # red -> black
    bnw[gray < threshold] = 0          # dark -> black
    bnw[blue_mask] = 255     # blue -> white (explicit map)

    return bnw


def ocr_digit(digit):
    digit = to_bnw(digit)
    digit = cv2.resize(digit, (16, 16), interpolation=cv2.INTER_AREA)

    text = ""
    for d, d_template in digit_images.items():
        match = image_utils.get_best_match(digit, d_template)
        if match > 0.8:
            text = d
            break
        
    if text == "":    
        text = pytesseract.image_to_string(
            digit, config=pytesseract_config
        ).strip()
    
    # wrong values
    if text == "1":  
        return ""
    if text == "0":  
        return ""
    if len(text) > 2:
        return ""
    if len(text) == 2 and text != "10":
        return ""
    if text == "10":
        return "T"
    return text