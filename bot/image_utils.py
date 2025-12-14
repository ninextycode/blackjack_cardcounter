import itertools
import cv2
import numpy as np
from PIL import Image
from collections import defaultdict


def load_rdb(img_path):
    return np.array(Image.open(img_path).convert("RGB"))


def save_rgb_png(img_rgb, img_path):
    img_pil = Image.fromarray(img_rgb)
    img_pil.save(img_path, format="PNG")
    

def find_subimages(main_img, templates, threshold=0.8):
    # Avoid list aliasing so each template has its own match list
    matches = defaultdict(list)
    n_matches = 0
    for key, template in templates.items():
        h = template.shape[0]
        w = template.shape[1]
        # Perform template matching
        result = cv2.matchTemplate(main_img, template, cv2.TM_CCOEFF_NORMED)
        filter_size = min(h, w)
        peaks = find_peaks(result, filter_size)
        result[~peaks] = 0
        # Find all matches above threshold
        locations = np.where(result >= threshold)
        
        locations_xy = locations[::-1]
        for pt in zip(*locations_xy):
            matches[key].append([
                np.array([pt[0], pt[1]]),
                np.array([pt[0] + w, pt[1] + h]),
                result[pt[1], pt[0]]
            ])
            n_matches += 1
    return matches, n_matches


def find_peaks(result, filter_size):
    # Create a dilated version (local maximum filter)
    kernel = np.ones((filter_size, filter_size), np.uint8)
    local_max = cv2.dilate(result, kernel)
    peaks = (result == local_max)
    return peaks


def wrap_perspective(image, points, dest_w, dest_h):
    # points must be ordered as top-left, bottom-left, bottom-right, top-right
    dst_points = np.array([
        [0, 0],                     # top-left
        [0, dest_h],                # bottom-left
        [dest_w, dest_h],           # bottom-right
        [dest_w, 0]                 # top-right
    ], dtype=np.float32)

    M = cv2.getPerspectiveTransform(points, dst_points)
    wrapped_subimage = cv2.warpPerspective(image, M, (dest_w, dest_h))
    return wrapped_subimage


def get_blue_mask(img_rgb):
    hsv = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2HSV)
    return get_blue_mask_hsv(hsv)


def get_blue_mask_hsv(hsv):
    # Blue range in HSV
    blue_mask = cv2.inRange(hsv, np.array([95, 80, 40]), np.array([140, 255, 255]))
    return blue_mask > 0

def get_white_mask(img_rgb):
    hsv = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2HSV)
    return get_white_mask_hsv(hsv)

def get_white_mask_hsv(hsv):
    lower_white = np.array([0, 0, 200])
    upper_white = np.array([180, 30, 255])
    mask = cv2.inRange(hsv, lower_white, upper_white)
    return mask > 0


def get_red_mask(img_rgb):
    hsv = cv2.cvtColor(img_rgb, cv2.COLOR_RGB2HSV)
    return get_red_mask_hsv(hsv)


def get_red_mask_hsv(hsv):
    # Red wraps around in HSV
    red1 = cv2.inRange(hsv, np.array([0, 70, 40]),  np.array([15, 255, 255]))
    red2 = cv2.inRange(hsv, np.array([165, 70, 40]), np.array([180, 255, 255]))
    red_mask = cv2.bitwise_or(red1, red2)
    return red_mask > 0


def get_best_match(image, template):
    result = cv2.matchTemplate(image, template, cv2.TM_CCOEFF_NORMED)
    return result.max()


def find_template_middle_positions(game_img, templates, threshold=0.8, allow_overlap=False):
    if not isinstance(templates, dict):
        templates = {i: t for i, t in enumerate(templates)}
    
    matches, n_matches = find_subimages(
        game_img, templates, threshold=threshold
    )
    mid_positions = []

    for match in itertools.chain.from_iterable(matches.values()):
        top_left = match[0]
        bottom_right = match[1]
        cross_area = game_img[top_left[1]:bottom_right[1], top_left[0]:bottom_right[0]]
        hsv = cv2.cvtColor(cross_area, cv2.COLOR_RGB2HSV)
        brightness = hsv[..., 2].mean()
        if brightness < 100: # inactive cross
            continue
        x_mid = (top_left[0] + bottom_right[0]) // 2
        y_mid = (top_left[1] + bottom_right[1]) // 2

        position_exists = False

        if not allow_overlap:
            width = bottom_right[0] - top_left[0]
            height = bottom_right[1] - top_left[1]
            for xp, yp in mid_positions:
                if np.abs(xp - x_mid) < width and np.abs(yp - y_mid) < height:
                    position_exists = True

        if not position_exists:
            mid_positions.append((x_mid, y_mid))
    
    return mid_positions
