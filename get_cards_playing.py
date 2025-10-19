import numpy as np
import image_utils
import ocr_cards

_suit_templates = []
_suits = "shcd"
for suit in _suits:
    _suit_templates.append(image_utils.load_rdb(f"suits/{suit}.png"))
card_peek_width = 68
card_height = 135
card_width = 150


def extract_card_values_img(player_cards_img, suit_matches):
    digits = [[] for i in range(4)]
    H, W = player_cards_img.shape[:2]

    for i, m_list in enumerate(suit_matches):
        for (tl, br, score) in m_list:
            left = int(min(tl[0], br[0]))
            right = br[0] + 10
            top = tl[1] - 42
            bottom = tl[1] + 4
            y0 = max(0, top)
            y1 = min(H, bottom)
            x0 = max(0, left)
            x1 = min(W, right)
            digits[i].append(player_cards_img[y0:y1, x0:x1])
    return digits


def get_player_cards_img(game_img):
    player_cards_x_slice = slice(800, 1500, None)
    player_cards_y_slice = slice(400, 675, None)
    return game_img[player_cards_y_slice, player_cards_x_slice]


def get_far_player_cards_img(game_img):
    player_cards_area = np.array([
        [1244, 484],
        [1249, 568],
        [1391, 568],
        [1382, 483],
    ], dtype=np.float32)
    desired_width = card_width + card_peek_width
    desired_height = card_height

    return image_utils.wrap_perspective(
        game_img, player_cards_area, 
        desired_width, desired_height
    )


def get_initial_dealer_cards_img(game_img):
    # Ordered as: top-left, bottom-left, bottom-right, top-right
    dealer_cards_area = np.array([
        [849, 135],
        [719, 191],
        [823, 259],
        [956, 196],
    ], dtype=np.float32)

    desired_width = card_width + card_peek_width
    desired_height = card_height

    return image_utils.wrap_perspective(
        game_img, dealer_cards_area, 
        desired_width, desired_height
    )

def get_final_dealer_cards_img(game_img):
    dealer_cards_area = np.array([
        [1086, 257],
        [995, 304],
        [1322, 484],
        [1413, 430],
    ], dtype=np.float32)

    # allow for 10 peeking cards and one whole
    desired_width = card_peek_width * 10 + card_width
    desired_height = card_height

    return image_utils.wrap_perspective(
        game_img, dealer_cards_area, 
        desired_width, desired_height
    )


def get_cards(card_area_img):
    matches, n_matches = \
        image_utils.find_subimages(card_area_img, _suit_templates, threshold=0.9)
    if n_matches == 0:
        return []
    digit_images = extract_card_values_img(card_area_img, matches)

    player_cards = []
    topleft = []
    for s, suit_digit_imgs, suit_matches in zip(_suits, digit_images, matches):
        for digit_img, match in zip(suit_digit_imgs, suit_matches):
            value_text = ocr_cards.ocr_digit(digit_img)
            if len(value_text) > 0:
                player_cards.append(value_text + s)
            # sort by a position, first by y axis, then by x
            topleft.append((match[0][1], match[0][0]))
    min_y = min(y for y, x in topleft)
    # replace y with line number with width = card_height / 4
    index_2d = []
    for tl in topleft:
        tl = ((tl[0] - min_y) // (card_height / 4), tl[1])
        index_2d.append(tl)
    player_cards_idx = sorted(range(len(player_cards)), key=lambda i: index_2d[i])
    return [player_cards[i] for i in player_cards_idx]


def get_player_cards(game_img):
    player_cards_img = get_player_cards_img(game_img)
    return get_cards(player_cards_img)

def get_far_player_cards(game_img):
    player_cards_img = get_far_player_cards_img(game_img)
    return get_cards(player_cards_img)

def get_initial_dealer_cards(game_img):
    dealer_cards_img = get_initial_dealer_cards_img(game_img)
    return get_cards(dealer_cards_img)


def get_final_dealer_cards(game_img):
    dealer_cards_img = get_final_dealer_cards_img(game_img)
    return get_cards(dealer_cards_img)
