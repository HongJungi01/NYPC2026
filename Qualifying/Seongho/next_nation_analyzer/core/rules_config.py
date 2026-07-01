"""Rule constants for NEXT NATION analysis.

Source note: the plan says these constants should mirror testing-tool.py.
Only values explicitly documented in the plan are hard requirements here;
level tables are conservative placeholders until testing-tool.py is available.
"""

RULE_VERSION = "v3.1"

INITIAL_GOLD = 500
TRAIN_COST = 120
MOVE_COST = 10

HQ_INITIAL_UNITS = {"A": ["A1", "A2", "A3"], "B": ["B1", "B2", "B3"]}

BUILDING_LEVELS = {
    1: {"upgrade_cost": 150, "income": 15, "worker_capacity": 1, "unit_hp": 4},
    2: {"upgrade_cost": 250, "income": 20, "worker_capacity": 2, "unit_hp": 5},
    3: {"upgrade_cost": 350, "income": 25, "worker_capacity": 3, "unit_hp": 6},
    4: {"upgrade_cost": 500, "income": 30, "worker_capacity": 4, "unit_hp": 7},
    5: {"upgrade_cost": 0, "income": 35, "worker_capacity": 5, "unit_hp": 8},
}

NEW_BUILDING_COST = BUILDING_LEVELS[1]["upgrade_cost"]
MAX_BUILDING_LEVEL = 5

