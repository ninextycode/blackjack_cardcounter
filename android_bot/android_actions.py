import logging
import uiautomator2 as u2
from blackjack.actions import PlayerAction
import time

logger = logging.getLogger(__name__)


class AndroidActor:
    def __init__(self, device_ip=None):
        self.device = u2.connect(device_ip)

    def tap(self, x: int, y: int):
        self.device.click(x, y)

    def swipe(self, x1: int, y1: int, x2: int, y2: int, duration: float = 0.5):
        self.device.swipe(x1, y1, x2, y2, duration)


class AndroidBJTabletActor:
    def __init__(self, bets, max_bet=None, device_ip=None):
        self.device = u2.connect(device_ip)
        
        self._btn_locs = [
            [1200, 1300],
            [1520, 1300],
            [1820, 1300],
            [2140, 1300]
        ]

        self._clear_bet_loc = self._btn_locs[1]
        self._deal_loc = self._btn_locs[3]
        
        self._refuse_insurance = self._btn_locs[1]
        self._take_insurance = self._btn_locs[2]

        self._split_loc = self._btn_locs[0]
        self._stand_loc = self._btn_locs[1]
        self._hit_loc = self._btn_locs[2]
        self._double_loc = self._btn_locs[3]
        
        self._player_loc = [1310, 1010]

        self._add_bet_loc = [1300, 765]
        self._rebuy_wheel_loc = [1300, 825]
        self._rebuy_play_btn_loc = [1150, 1135]
        self._rebuy_ok_btn_loc = [1150, 880]
        
        self._tournament_ad_continue = [1150, 1330]

        self._leave_table_loc = [2170, 160]
        self._leave_table_yet_loc = [1350, 880]
        self._leave_table_exit_loc = [1370, 1060]

        self._create_private_table_loc = [370, 380]
        self._bet_locs = [
            [1125, 800],
            [1212, 815],
            [1305, 820],
            [1395, 813],
            [1480, 800],
        ]
        self._min_bet = min(bets)
        self._max_bet = max(bets) if max_bet is None else max_bet
        self._max_bet_units = self._max_bet / self._min_bet
        self._bet_value_units = [b / self._min_bet for b in bets]
        self._bet_values = list(bets)
        self._click_sleep_time = 0.2

    def take_action(self, action):
        if action == PlayerAction.DOUBLE:
            self.double()
        elif action == PlayerAction.SPLIT:
            self.split()
        elif action == PlayerAction.HIT:
            self.hit()
        elif action == PlayerAction.STAND:
            self.stand()
        elif action == PlayerAction.REFUSE_INSURANCE:
            self.refuse_insurance()
        elif action == PlayerAction.TAKE_INSURANCE:
            self.take_insurance()
        else:
            raise ValueError(f"Unsupported action {action}")

    def click(self, x, y):
        x, y = float(x), float(y)
        logger.info(f"click at {x}, {y}")
        self.device.click(x, y)
        self.sleep(self._click_sleep_time)

    def sleep(self, sleep_time):
        self.device.sleep(sleep_time)

    def deal(self):
        logger.info("deal")
        self.click(*self._deal_loc)

    def split(self):
        logger.info("split")
        self.click(*self._split_loc)
    
    def stand(self):
        logger.info("stand")
        self.click(*self._stand_loc)

    def hit(self):
        logger.info("hit")
        self.click(*self._hit_loc)

    def double(self):
        logger.info("double")
        self.click(*self._double_loc)

    def place_min_bet(self):
        self.click(*self._bet_locs[0])
        self.click(*self._add_bet_loc)

    def place_max_bet(self):
        self.click(*self._bet_locs[-1])
        self.click(*self._add_bet_loc)

    def clear_bet(self):
        self.click(*self._clear_bet_loc)

    def place_bet(self, amount):
        self.place_bet_units(amount / self._min_bet)

    def take_insurance(self):
        self.click(*self._take_insurance)
    
    def refuse_insurance(self):
        self.click(*self._refuse_insurance)

    def leave_table(self):
        self.click(*self._leave_table_loc)
        # self.sleep(1.5)
        # # immediate clicks
        # self.device.click(*self._leave_table_yet_loc)
        # self.device.click(*self._leave_table_exit_loc)

    def create_private_table(self):
        self.click(*self._create_private_table_loc)

    def place_bet_units(self, bet_units):
        if int(bet_units) != bet_units:
            raise ValueError(f"Bet amount must be multiple of 0.5 units")
        if not (1 <= bet_units <= self._max_bet_units):
            raise ValueError(f"Bet amount must be between {self._min_bet} and {self._max_bet}")

        self.clear_bet()
        
        while bet_units > 0:
            success = False
            for i in reversed(range(len(self._bet_value_units))):
                bet_val_units = self._bet_value_units[i]
                if bet_val_units > bet_units:
                    continue
                bet_loc = self._bet_locs[i]
                self.sleep(1)
                self.click(*bet_loc)
                self.sleep(1)
                self.click(*self._add_bet_loc)
                bet_units -= bet_val_units
                success = True
                break
            if not success:
                raise RuntimeError("Failed to place bet")
    

    def start_rebuy(self):
        self.click(*self._player_loc)
        self.click(*self._rebuy_wheel_loc)