import numpy as np
from android_bot import image_utils
from android_bot import ocr_cards
from enum import Enum
import cv2
from android_bot.image_assets import ImageAssets
import itertools


card_width = 100
card_peek_width = 40
card_height = 120
card_peek_height = 40

middle_cards_poly = np.array([
    [1222, 624],
    [1225, 728],
    [1410, 728],
    [1400, 622]
], dtype=np.float32)

left_cards_poly = np.array([
    [1135.,  623.],
    [1135.,  728.],
    [1320.,  728.],
    [1313.,  623.]
], dtype=np.float32)

right_cards_poly = np.array([
    [1321,  623],
    [1328,  726],
    [1515,  726],
    [1498,  622]
], dtype=np.float32)

dealer_cards_poly = np.array([
    [1100, 420],
    [1024, 463],
    [1278, 627],
    [1358, 576]
], dtype=np.float32)

digit_width = 36
digit_height = 34
digit_x_offset = 0
digit_y_offset = 2



def extract_card_values_img(player_cards_img, suit_matches):
    digits = {s: [] for s in ImageAssets.suit_templates.keys()}
    H, W = player_cards_img.shape[:2]

    for s, m_list in suit_matches.items():
        for (top_left, bot_right, score) in m_list:
            left = top_left[0] + digit_x_offset
            right = top_left[0] + digit_width + digit_x_offset
            top = top_left[1] - digit_height - digit_y_offset
            bottom = top_left[1] - digit_y_offset
            y0 = max(0, top)
            y1 = min(H, bottom)
            x0 = max(0, left)
            x1 = min(W, right)
            digits[s].append(player_cards_img[y0:y1, x0:x1])
    return digits


def get_player_cards_left(game_img):
    cards_img = get_player_cards_left_img(game_img)
    return get_cards(cards_img) 


def get_player_cards_left_img(game_img):
    desired_width = card_width + 3 * card_peek_width
    desired_height = card_height + card_peek_height
    return image_utils.wrap_perspective(
        game_img, left_cards_poly, 
        desired_width, desired_height
    )


def get_player_cards_middle(game_img):
    cards_img = get_player_cards_middle_img(game_img)
    return get_cards(cards_img) 


def get_player_cards_middle_img(game_img):
    desired_width = card_width + 3 * card_peek_width
    desired_height = card_height + card_peek_height
    return image_utils.wrap_perspective(
        game_img, middle_cards_poly, 
        desired_width, desired_height
    )


def get_player_cards_right(game_img):
    cards_img = get_player_cards_right_img(game_img)
    return get_cards(cards_img) 


def get_player_cards_right_img(game_img):
    desired_width = card_width + 3 * card_peek_width
    desired_height = card_height + card_peek_height
    return image_utils.wrap_perspective(
        game_img, right_cards_poly, 
        desired_width, desired_height
    )


def is_split_pair(game_img):
    # line between split cards should be 
    # blue - table color
    idx = (slice(630, 691), slice(1318, 1323))
    segment = game_img[idx]
    mask = image_utils.get_blue_mask(segment)
    blue_ratio = mask.sum() / (segment.shape[0] * segment.shape[1])
    return blue_ratio > 0.75


def get_dealer_cards_img(game_img): 
    desired_width = card_width + card_peek_width * 10
    desired_height = card_height
    return image_utils.wrap_perspective(
        game_img, dealer_cards_poly, 
        desired_width, desired_height
    )


def get_cards(card_area_img):
    matches, n_matches = \
        image_utils.find_subimages(
            card_area_img, ImageAssets.suit_templates,
            threshold=0.9
        )
    if n_matches == 0:
        return []
    digit_images = extract_card_values_img(card_area_img, matches)

    player_cards = []
    topleft = []
    for s in ImageAssets.suit_templates.keys():
        suit_digit_imgs = digit_images[s]
        suit_matches = matches[s]
        for digit_img, match in zip(suit_digit_imgs, suit_matches):
            value_text = ocr_cards.ocr_digit(digit_img)
            if len(value_text) == 0:
                continue
            player_cards.append(value_text + s)
            # sort by a position, first by y axis, then by x
            topleft.append((match[0][1], match[0][0]))
    min_y = min(y for y, x in topleft) if len(topleft) > 0 else 0
    # replace y with line number with width = card_height / 4
    index_2d = []
    for tl in topleft:
        tl = ((tl[0] - min_y) // (card_height / 4), tl[1])
        index_2d.append(tl)
    player_cards_idx = sorted(range(len(player_cards)), key=lambda i: index_2d[i])
    return [player_cards[i] for i in player_cards_idx]


def get_dealer_cards(game_img):
    dealer_cards_img = get_dealer_cards_img(game_img)
    return get_cards(dealer_cards_img)


def get_player_cards(game_img):
    is_pair = is_split_pair(game_img)
    if is_pair:
        left_img = get_player_cards_left(game_img)
        right_img = get_player_cards_right(game_img)
        left_cards = get_cards(left_img)
        right_cards = get_cards(right_img)
        return left_cards, right_cards
    else:
        middle_img = get_player_cards_middle(game_img)
        return get_cards(middle_img)


def is_insurance_offered(game_img):
    insurance_area = game_img[675:728, 1185:1411]
    match = image_utils.get_best_match(
        insurance_area, ImageAssets.insurance_offer
    )
    return match > 0.9


class HandPosition(Enum):
    LEFT = 1
    RIGHT = 2
    MIDDLE = 3
    NONE = 4



def get_active_hand(game_img):
    finger_area = (slice(545, 620), slice(1150, 1400))
    finger_img = game_img[finger_area]
    match_middle = image_utils.get_best_match(
        finger_img, ImageAssets.finger_middle
    )
    if match_middle > 0.9:
        return HandPosition.MIDDLE
    
    match_middle_split = image_utils.get_best_match(
        finger_img, ImageAssets.finger_middle_split
    )
    if match_middle_split > 0.9:
        return HandPosition.MIDDLE
    
    match_left = image_utils.get_best_match(
        finger_img, ImageAssets.finger_left
    )
    if match_left > 0.9:
        return HandPosition.LEFT
    
    match_right = image_utils.get_best_match(
        finger_img, ImageAssets.finger_right
    )
    if match_right > 0.9:
        return HandPosition.RIGHT

    return HandPosition.NONE


def get_player_cards(game_img, hand: HandPosition):
    if hand == HandPosition.LEFT:
        return get_player_cards_left(game_img)
    elif hand == HandPosition.RIGHT:
        return get_player_cards_right(game_img)
    elif hand == HandPosition.MIDDLE:
        return get_player_cards_middle(game_img)
    else:
        return None


def is_tournament_ad(game_img):
    ad_continue_btn = ImageAssets.ad_continue_btn
    ad_continue_btn_area = (slice(1300,1375), slice(900,1400))
    area_img = game_img[ad_continue_btn_area]
    match = image_utils.get_best_match(
        area_img, ad_continue_btn
    )
    return match > 0.9


def is_table_empty(game_img):
    empty_table = ImageAssets.empty_table
    empty_table_2 = ImageAssets.empty_table_2
    empty_table_area = (slice(550, 775), slice(1000, 1375))
    area_img = game_img[empty_table_area]
    match_2 = image_utils.get_best_match(
        area_img, empty_table_2
    )
    if match_2 > 0.9:
        return True
    
    match = image_utils.get_best_match(
        area_img, empty_table
    )
    return match > 0.9


def is_pre_shuffle(game_img):
    red_card_area = (slice(260, 286), slice(985, 1021))
    red_mean = image_utils.get_red_mask(game_img[red_card_area]).mean()
    blue_mean = image_utils.get_blue_mask(game_img[red_card_area]).mean()
    return red_mean > 0.975 and blue_mean < 0.025


def get_shoe_penetration(game_img):
    shoe_poly = np.array([
        [1663, 471],
        [1564, 492],
        [1590, 556],
        [1691, 533]
    ], dtype=np.float32)
    
    height = 201
    width = 100
    wrapped_shoe = image_utils.wrap_perspective(
        game_img, shoe_poly, dest_w=width, dest_h=height
    )

    white_mask = image_utils.get_white_mask(wrapped_shoe)
    white_mean = white_mask.mean(axis=1)
    top_card_idx = height - 1

    for idx, val in enumerate(white_mean):
        if val > 0.9:
            top_card_idx = idx
            break
    
    penetration = 1 - ((height-1) - top_card_idx) / (height-1)
    if penetration == 1:
        raise RuntimeError("Unexpected shoe penetration value")
    return penetration


def can_hit_stand(game_img):
    stand_btn_region = game_img[1265:1321, 1400:1450]
    hsv = cv2.cvtColor(stand_btn_region, cv2.COLOR_RGB2HSV)
    # button is bright enough
    return np.mean(hsv[..., 2] > 100) > 0.9


def can_create_private_table(game_img):
    private_table_area = (slice(325, 460), slice(250, 450))
    area_img = game_img[private_table_area]

    private_table = ImageAssets.private_table
    match_0 = image_utils.get_best_match(
        area_img, private_table
    )
    hsv = cv2.cvtColor(area_img, cv2.COLOR_RGB2HSV)
    brightness = hsv[..., 2].mean()
    return match_0 > 0.9 and brightness > 100


def find_close_ad_crosses(game_img):
    templates = {
        "cross": ImageAssets.ad_cross,
        "pink_cross": ImageAssets.ad_cross_pink,
        "small_cross": ImageAssets.ad_cross_small
    }
    return image_utils.find_template_middle_positions(
        game_img, templates, threshold=0.8
    )


def find_leave_table_button(game_img):
    templates = {
        "yes": ImageAssets.leave_table_yes,
        "exit": ImageAssets.leave_table_exit
    }
    return image_utils.find_template_middle_positions(
        game_img, templates
    )