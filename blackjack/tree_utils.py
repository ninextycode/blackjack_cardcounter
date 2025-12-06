from collections import defaultdict, deque
from collections import Counter
from typing import Generator
from blackjack.abstract_node import AbstractBJTreeNode, ValueNode
from blackjack.blackjack_round import BJStage
from blackjack.abstract_node import FloorCeilValueNode
from blackjack.abstract_node import ValueNode


def get_nodes_by_levels(root_node, nodes_by_level_dict=None):
    if nodes_by_level_dict is None:
        nodes_by_level_dict = defaultdict(list)
    queue = deque()
    queue.append((root_node, 0))
    while queue:
        node, node_level = queue.popleft()
        # handle case of a node with 2 parents, up to level 4
        if node_level < 4 and node in nodes_by_level_dict[node_level]:
            continue
        nodes_by_level_dict[node_level].append(node)
        for child in node.children:
            queue.append((child, node_level + 1))
    return nodes_by_level_dict



def iterate_nodes_by_levels(root_node) -> Generator[tuple[int, AbstractBJTreeNode], None, None]:
    nodes_by_level_dict = defaultdict(list)
    queue = deque()
    queue.append((0, root_node))
    while queue:
        node_level, node = queue.popleft()
        # handle case of a node with 2 parents, up to level 4
        if node_level < 4 and node in nodes_by_level_dict[node_level]:
            continue  
        nodes_by_level_dict[node_level].append(node)
        yield node_level, node
        for child in node.children:
            queue.append((node_level + 1, child))


def log_tree_structure(root_node):
    nodes_dict = get_nodes_by_levels(root_node)
    for lvl, nodes in nodes_dict.items():
        stage_count = count_stages(nodes)
        print(f"Level {lvl}: {len(nodes)} nodes", stage_count)


def count_stages(node_list):
    stage_counter = Counter()
    for node in node_list:
        if isinstance(node, ValueNode):
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


def format_tree_output(root_node, n_levels, indent_size=2):
    """
    Format a nice hierarchical output of the game tree.
    
    Parameters:
    - root_node: The root node of the tree
    - n_levels: Number of levels to display (0 = just root, 1 = root + children, etc.)
    - indent_size: Number of spaces per indentation level
    
    Returns:
    - Formatted string representation of the tree
    """
    from blackjack.floor_ceil_node import DecisionNode
    
    lines = []
    
    def get_node_type_name(node):
        """Get a short name for the node type."""
        class_name = node.__class__.__name__
        return class_name.replace("Node", "")
    
    def format_value(val):
        """Format a numeric value."""
        if val is None:
            return "N/A"
        return f"{val:+.3f}"
    
    def get_best_action_idx(node):
        """Get the index of the best action for a decision node."""
        if not isinstance(node, DecisionNode):
            return None
        if not node.has_built_children:
            return None
        # Best action is the one with probability 1
        for i, p in enumerate(node.children_prob):
            if p == 1:
                return i
        return None
    
    def format_event(event):
        """Format an event (action or card) for display."""
        if event is None:
            return ""
        if hasattr(event, 'name'):  # Enum
            return event.name
        if isinstance(event, int):  # Card rank value
            if event == 10:
                return "T"
            elif event == 11:
                return "A"
            else:
                return str(event)
        return str(event)
    
    def format_node_line(node, event=None, is_best=False, level=0):
        """Format a single node line."""
        indent = " " * (indent_size * level)
        marker = "* " if is_best else "  "
        
        # Get values
        try:
            value = node.get_value() if hasattr(node, 'get_value') else None
        except:
            value = None
        
        try:
            floor_val = node.get_floor_value() if hasattr(node, 'get_floor_value') else None
        except:
            floor_val = None
        
        try:
            ceil_val = node.get_ceil_value() if hasattr(node, 'get_ceil_value') else None
        except:
            ceil_val = None
        
        # Build the line
        node_type = get_node_type_name(node)
        event_str = f"[{format_event(event)}] " if event is not None else ""
        
        # Get stage info for non-value nodes
        if isinstance(node, ValueNode):
            stage_str = "Terminal"
        elif isinstance(node, FloorCeilValueNode):
            stage_str = "<FloorCeilPlaceholder>"
        else:
            stage = node.bj_round.get_stage()
            stage_str = stage.name if hasattr(stage, 'name') else str(stage)
        
        # Format values
        value_str = f"EV={format_value(value)}"
        if floor_val is not None and ceil_val is not None:
            value_str += f" [{format_value(floor_val)}, {format_value(ceil_val)}]"
        
        # Add decision info for DecisionNode
        extra_info = ""
        if isinstance(node, DecisionNode):
            if node.has_decided():
                extra_info = f" -> {node.decision_choice.name}"
            else:
                actions = [a.name for a in node.possible_actions]
                extra_info = f" (possible: {', '.join(actions)})"
        
        return f"{indent}{marker}{event_str}{node_type}({stage_str}) {value_str}{extra_info}"
    
    def traverse(node, event=None, is_best=False, level=0):
        """Recursively traverse and format the tree."""
        lines.append(format_node_line(node, event, is_best, level))
        
        if level >= n_levels:
            return
        
        if isinstance(node, ValueNode):
            return
        
        if not node.has_built_children or not node.children:
            return
        
        best_idx = get_best_action_idx(node)
        
        for i, (child, child_event) in enumerate(zip(node.children, node.children_events)):
            child_is_best = (i == best_idx)
            traverse(child, child_event, child_is_best, level + 1)
    
    traverse(root_node)
    return "\n".join(lines)


def print_tree(root_node, n_levels=2, indent_size=2):
    """Print the formatted tree output."""
    print(format_tree_output(root_node, n_levels, indent_size))
