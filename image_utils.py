import cv2
import numpy as np
from PIL import Image


def load_rdb(img_path):
    return np.array(Image.open(img_path).convert("RGB"))


def save_rgb_png(img_rgb, img_path):
    img_pil = Image.fromarray(img_rgb)
    img_pil.save(img_path, format="PNG")
    

def find_subimages(main_img, templates, threshold=0.8):
    """
    Find multiple template images in main image
    
    Args:
        main_img_path: Path to main image
        template_paths: List of paths to template images
        threshold: Matching confidence (0-1)
    
    Returns:
        List of matches with locations
    """    
    # Avoid list aliasing so each template has its own match list
    matches = [[] for _ in range(len(templates))]
    n_matches = 0
    for i, template in enumerate(templates):
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
            matches[i].append([
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
