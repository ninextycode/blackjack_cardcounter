from android_bot import image_utils
import os
from pathlib import Path

class ImageAssets:
    root_dir = Path(__file__).parent.parent

    suit_templates = {}
    suits = "shcd"
    for suit in suits:
        suit_templates[suit] = image_utils.load_rdb(root_dir / f"suits_tablet/{suit}.png")
    
    insurance_offer = image_utils.load_rdb(root_dir / "ui_elements/insurance.png")

    finger_middle = image_utils.load_rdb(root_dir / "ui_elements/finger_middle.png")
    finger_middle_split = image_utils.load_rdb(root_dir / "ui_elements/finger_middle_split.png")
    finger_left = image_utils.load_rdb(root_dir / "ui_elements/finger_left.png")
    finger_right = image_utils.load_rdb(root_dir / "ui_elements/finger_right.png")

    ad_continue_btn = image_utils.load_rdb(root_dir / "ui_elements/ad_continue_btn.png")
    empty_table = image_utils.load_rdb(root_dir / "ui_elements/empty_table.png")
    empty_table_2 = image_utils.load_rdb(root_dir / "ui_elements/empty_table_2.png")

    private_table = image_utils.load_rdb(root_dir / "ui_elements/private_table.png")
    private_table_grey = image_utils.load_rdb(root_dir / "ui_elements/private_table_grey.png")

    ad_cross = image_utils.load_rdb(root_dir / "ui_elements/ad_cross.png")
    ad_cross_small = image_utils.load_rdb(root_dir / "ui_elements/ad_cross_small.png")
    ad_cross_pink = image_utils.load_rdb(root_dir / "ui_elements/ad_cross_pink.png")

    leave_table_exit = image_utils.load_rdb(root_dir / "ui_elements/leave_table_exit.png")
    leave_table_yes = image_utils.load_rdb(root_dir / "ui_elements/leave_table_yes.png")

    rebuy_confirm_1 = image_utils.load_rdb(root_dir / "ui_elements/rebuy_confirm_1.png")
    rebuy_confirm_2 = image_utils.load_rdb(root_dir / "ui_elements/rebuy_confirm_2.png")