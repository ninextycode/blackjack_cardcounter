from collections import defaultdict, deque
from collections import Counter
from blackjack.game_node import SimulationResultNode
from blackjack.blackjack_round import BJStage



def iterate_nodes_by_levels(root_node):
    queue = deque()
    queue.append((0, root_node))
    while queue:
        node_level, node = queue.popleft()
        yield node_level, node
        for child in node.children:
            queue.append((node_level + 1, child))


def get_nodes_by_levels(root_node, nodes_by_level_dict=None):
    if nodes_by_level_dict is None:
        nodes_by_level_dict = defaultdict(list)
    queue = deque()
    queue.append((root_node, 0))
    while queue:
        node, node_level = queue.popleft()
        nodes_by_level_dict[node_level].append(node)
        for child in node.children:
            queue.append((child, node_level + 1))
    return nodes_by_level_dict


def log_tree_structure(root_node):
    nodes_dict = get_nodes_by_levels(root_node)
    for lvl, nodes in nodes_dict.items():
        stage_count = count_stages(nodes)
        print(f"Level {lvl}: {len(nodes)} nodes", stage_count)


def count_stages(node_list):
    stage_counter = Counter()
    for node in node_list:
        if isinstance(node, SimulationResultNode):
            stage = BJStage.ROUND_OVER
        else:
            stage = node.bj_round.get_stage()
        
        if stage == BJStage.PLAYER_CARD:
            stage_name = "PLAYER_CARD"
            if len(node.children) == 1:
                stage_name += "_SINGLE"
            elif len(node.children) < 10:
                stage_name += "_PARTIAL"
            else:
                stage_name += "_FULL"
        else:
            stage_name = stage.name
        stage_counter[stage_name] += 1
    return stage_counter
