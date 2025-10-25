import json
from blackjack.game_tree import SimulationResultNode
from blackjack.blackjack_round import BJStage



def iterate_through_all_nodes(root_node):
    stack = [root_node]
    while stack:
        node = stack.pop()
        yield node
        if hasattr(node, 'children'):
            stack.extend(node.children)


def game_tree_to_json(file_obj, root_node):
    nodes = [n for n in iterate_through_all_nodes(root_node)]
    # Map each node's raw address/id to its index in the nodes list
    address_to_idx = { id(n): i for i, n in enumerate(nodes) }

    # Build edges using indices (use empty list if no children)
    edges = [
        (address_to_idx[id(parent)], address_to_idx[id(child)])
        for parent in nodes for child in getattr(parent, "children", [])
    ]

    file_obj.write(
        """{\n"""
        """  "nodes": [\n"""
    )

    for i, n in enumerate(nodes):
        node_type = None
        if isinstance(n, SimulationResultNode):
            node_type = "Terminal"
            representation = f"SimulationResult: value = {n.value}"
        else:
            bj_round = n.bj_round
            stage = bj_round.stage
            representation = str(bj_round)
            if stage in (
                BJStage.PLAYER_ACTION,
                BJStage.PLAYER_OFFERED_EARLY_SURRENDER,
                BJStage.PLAYER_OFFERED_INSURANCE
            ):
                node_type = "Decision"
            elif stage in (
                BJStage.PLAYER_CARD,
                BJStage.DEALER_CARD,
                BJStage.DEALER_CHECK_BJ
            ):
                node_type = "Chance"
            elif stage == BJStage.ROUND_OVER:
                node_type = "Terminal"
            else:
                raise RuntimeError(f"Unknown stage {stage}")
        representation = representation.replace("\n", "\\n")

        file_obj.write(
            f"""    {{ "id": "{i}", """
            f""""type": "{node_type}", """
            f""""representation": "{representation}" }}"""
        )
        if i < len(nodes) - 1:
            file_obj.write(",\n")
        else:
            file_obj.write("\n")

    file_obj.write(
        """  ],\n"""
        """  "edges": [\n"""
    )

    for i, (src_idx, dst_idx) in enumerate(edges):
        action_str = ""
        file_obj.write(
            f"""    {{ "source": "{src_idx}", "target": "{dst_idx}", "action_string": "{action_str}" }}"""
        )
        if i < len(edges) - 1:
            file_obj.write(",\n")
        else:
            file_obj.write("\n")

    file_obj.write(
        """  ]\n"""
        """}\n"""
    )