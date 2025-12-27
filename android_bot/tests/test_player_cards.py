import unittest
from android_bot import image_assets
from android_bot import get_cards_tablet
from android_bot import image_utils
from android_bot import ocr_cards


class TestPlayerCards(unittest.TestCase):
    def test_ocr_cards(self):
        player_cards_map = {
            "2c6h4d_7s": ["2c", "6h", "4d"],
            "7sQd_Jh": ["7s", "Qd"],
            "9sTd_Qh": ["9s", "Td"],
            "Ts6d_3d": ["Ts", "6d"],
            "dealer_3_cards": ["4s", "Ts"],
            "dealer_4_cards_2": ["2h", "8c", "9c"],
            "dealer_4_cards": ["Qd", "7h"],
            "dealer_5_cards": ["3c", "Ac", "7c"],
            "pair_bust_3": [["7s", "5s"], ["5s", "Kh", "2d"]],
            "pair_bust_before_removal": [["2d", "Td"], ["8h", "Jh"]],
            "pair_bust": [["Jd", "Tc"], []],
            "player_4_cards": ["4h", "2s", "5c", "Qc"],
            "player_5_cards": ["2c", "7h"],
            "player_blackjack": ["Ac", "Jc"],
            "split_1": [["6c", "5c"], ["6s", "Ts", "3c"]],
            "split_2": [["6c", "5c", "7d", "Ah"], ["6s", "Ts", "3c", "2h"]],
            "split_3": [["6c", "5c", "7d", "Ah"], ["6s", "Ts", "3c", "2h"]],
            "split_5": [["2d", "Td"], ["2d", "8h"]],
            "Th4c_8d": ["Th", "4c"]
        }

        for name, expected_result in player_cards_map.items():
            path = image_assets.ImageAssets.root_dir / "frames_tablet" / f"{name}.png"
            img_test = image_utils.load_rdb(path)

            is_split = get_cards_tablet.is_split_pair(img_test)

            cards_values = []
            if is_split:
                cards_left = get_cards_tablet.get_player_cards_left_img(img_test)
                cards_right = get_cards_tablet.get_player_cards_right_img(img_test)
                cards_values.append(get_cards_tablet.get_cards(cards_left))
                cards_values.append(get_cards_tablet.get_cards(cards_right))
            else:
                cards_mid = get_cards_tablet.get_player_cards_middle_img(img_test)
                cards_values.extend(get_cards_tablet.get_cards(cards_mid))

            with self.subTest(name=name):
                # print(cards_values, expected_result)
                self.assertListEqual(cards_values, expected_result)
