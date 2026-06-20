# ================================
# Game class: manage game state
# ================================
class Game:

    def __init__(self, board, first):
        self.board = board            # Game board (2D array)
        self.first = first            # Whether this is the first player
        self.passed = False           # Whether the last turn was a pass

    # Checks if rectangle (r1, c1) ~ (r2, c2) is valid (sum is 10 and includes all four edges)
    def is_valid(self, r1, c1, r2, c2):
        sums = 0
        r1fit = c1fit = r2fit = c2fit = False

        for r in range(r1, r2 + 1):
            for c in range(c1, c2 + 1):
                if self.board[r][c] != 0:
                    sums += self.board[r][c]
                    if r == r1:
                        r1fit = True
                    if r == r2:
                        r2fit = True
                    if c == c1:
                        c1fit = True
                    if c == c2:
                        c2fit = True
        return sums == 10 and r1fit and r2fit and c1fit and c2fit

    # ================================================================
    # ===================== [REQUIRED IMPLEMENTATION] ===============================
    # Find a valid rectangle with sum 10 and return (r1, c1, r2, c2) as a tuple
    # Return (-1, -1, -1, -1) if none exists (meaning pass)
    # ================================================================
    def calculate_move(self, _my_time, _opp_time):
        # Strategy: select two horizontally adjacent cells if valid
        for r1 in range(len(self.board)):
            for c1 in range(len(self.board[r1]) - 1):
                r2 = r1
                c2 = c1 + 1
                if self.is_valid(r1, c1, r2, c2):
                    return (r1, c1, r2, c2)
        return (-1, -1, -1, -1)
    # =================== [REQUIRED IMPLEMENTATION END] =============================

    # Apply opponent's move to the board
    def update_opponent_action(self, action, _time):
        self.update_move(*action, False)

    # Apply the given move to the board (set cells to 0)
    def update_move(self, r1, c1, r2, c2, _is_my_move):
        if r1 == c1 == r2 == c2 == -1:
            self.passed = True
            return
        for r in range(r1, r2 + 1):
            for c in range(c1, c2 + 1):
                self.board[r][c] = 0
        self.passed = False


# ================================
# main(): handle I/O and run the game
# ================================
def main():
    first = False
    game = None
    while True:
        line = input().split()

        if len(line) == 0:
            continue

        command, *param = line

        if command == "READY":
            # Check if this player goes first
            turn = param[0]
            first = turn == "FIRST"
            print("OK", flush=True)
            continue

        if command == "INIT":
            # Initialize the board
            board = [list(map(int, row)) for row in param]
            game = Game(board, first)
            continue

        if command == "TIME":
            # My turn: calculate and execute move
            my_time, opp_time = map(int, param)
            ret = game.calculate_move(my_time, opp_time)
            game.update_move(*ret, True)
            print(*ret, flush=True)
            continue

        if command == "OPP":
            # Apply opponent's turn
            r1, c1, r2, c2, time = map(int, param)
            game.update_opponent_action((r1, c1, r2, c2), time)
            continue

        if command == "FINISH":
            break

        raise ValueError(f"Invalid command {command}")


if __name__ == "__main__":
    main()
