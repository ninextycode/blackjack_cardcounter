import unittest
from android_bot import image_assets
from android_bot import get_cards_tablet
from android_bot import image_utils
from android_bot import ocr_cards


class TestDealerCards(unittest.TestCase):
    def test_ocr_cards(self):
        dealer_cards_map = {
            "2c6h4d_7s": ["7s"],
            "dealer_Jh7h": ["Jh", "7h"],
            "3d5h4c7cv5dAsTdQh_error": ["5d", "As", "Td", "Qh"],
            "7sQd_Jh": ["Jh"],
            "9sTd_Qh": ["Qh"],
            "8hJd_7h": ["7h"],
            "AcJd_As3s": ["As", "3s"], 
            "Ts4d_2h": ["2h"],
            "2d4h_7h": ["7h"],
            "dealer_3_cards": ["4s", "4c", "Ks"],
            "dealer_4_cards_2": ["4h", "3h", "6d", "9c"],
            "dealer_4_cards": ["6s", "9s", "As", "Jh"],
            "dealer_5_cards": ["3h", "4d", "4d", "4s", "2c"],
            "pair_bust_3": ["2c"],
            "pair_bust_before_removal": ["Qs"],
            "pair_bust": ["9c"],
            "player_blackjack": ["9h", "Kc"],
            "split_1": ["6c"],
            "split_3": ["6c", "3s", "Ad"],
            "split_5": ["Qs"],
            "Th4c_8d": ["8d"],
        }

        for name, expected_result in dealer_cards_map.items():
            path = image_assets.ImageAssets.root_dir / "frames_tablet" / f"{name}.png"
            img_test = image_utils.load_rdb(path)

            cards_img = get_cards_tablet.get_dealer_cards_img(img_test)
            cards_values = get_cards_tablet.get_cards(cards_img)

            with self.subTest(name=name):
                # print(cards_values, expected_result)
                self.assertListEqual(cards_values, expected_result)