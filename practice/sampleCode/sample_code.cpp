#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>

using namespace std;

// Class that manages game state
class Game
{
private:
    vector<vector<int>> board; // Game board (2D vector)
    bool first;                // Whether this is the first player
    bool passed;               // Whether the last turn was a pass

public:
    Game() = default;

    Game(const vector<vector<int>> &board, bool first)
        : board(board), first(first), passed(false) {}

    // Checks if rectangle (r1, c1) ~ (r2, c2) is valid (sum is 10 and includes all four edges)
    bool isValid(int r1, int c1, int r2, int c2)
    {
        int sums = 0;
        bool r1fit = false, c1fit = false, r2fit = false, c2fit = false;

        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                if (board[r][c] != 0)
                {
                    sums += board[r][c];
                    if (r == r1)
                        r1fit = true;
                    if (r == r2)
                        r2fit = true;
                    if (c == c1)
                        c1fit = true;
                    if (c == c2)
                        c2fit = true;
                }
        return (sums == 10) && r1fit && r2fit && c1fit && c2fit;
    }

    // ================================================================
    // ===================== [REQUIRED IMPLEMENTATION] ===============================
    // Find a valid rectangle with sum 10 and return {r1, c1, r2, c2} as a vector
    // Return {-1, -1, -1, -1} if none exists (meaning pass)
    // ================================================================
    vector<int> calculateMove(int myTime, int oppTime)
    {
        // Strategy: select two horizontally adjacent cells if valid
        for (int r1 = 0; r1 < board.size(); r1++)
            for (int c1 = 0; c1 < board[r1].size() - 1; c1++)
            {
                int r2 = r1;
                int c2 = c1 + 1;
                if (isValid(r1, c1, r2, c2))
                    return {r1, c1, r2, c2};
            }
        return {-1, -1, -1, -1}; // No valid rectangle found, so pass
    }
    // =================== [REQUIRED IMPLEMENTATION END] =============================

    // Apply opponent's move to the board
    void updateOpponentAction(const vector<int> &action, int time)
    {
        updateMove(action[0], action[1], action[2], action[3], false);
    }

    // Apply the given move to the board (set cells to 0)
    void updateMove(int r1, int c1, int r2, int c2, bool isMyMove)
    {
        if (r1 == -1 && c1 == -1 && r2 == -1 && c2 == -1)
        {
            passed = true;
            return;
        }
        for (int r = r1; r <= r2; r++)
            for (int c = c1; c <= c2; c++)
                board[r][c] = 0;
        passed = false;
    }
};

// Main function that processes commands from stdin
int main()
{
    Game game;
    bool first = false;

    while (true)
    {
        string line;
        getline(cin, line);

        istringstream iss(line);
        string command;
        if (!(iss >> command))
            continue;

        if (command == "READY")
        {
            // Check if this player goes first
            string turn;
            iss >> turn;
            first = (turn == "FIRST");
            cout << "OK" << endl;
            continue;
        }

        if (command == "INIT")
        {
            // Initialize the board
            vector<vector<int>> board;
            string row;
            while (iss >> row)
            {
                vector<int> boardRow;
                for (char c : row)
                {
                    boardRow.push_back(c - '0'); // Character to number conversion
                }
                board.push_back(boardRow);
            }
            game = Game(board, first);
            continue;
        }

        if (command == "TIME")
        {
            // My turn: calculate and output move
            int myTime, oppTime;
            iss >> myTime >> oppTime;

            vector<int> ret = game.calculateMove(myTime, oppTime);
            game.updateMove(ret[0], ret[1], ret[2], ret[3], true);

            cout << ret[0] << " " << ret[1] << " " << ret[2] << " " << ret[3] << endl; // Output my move
            continue;
        }

        if (command == "OPP")
        {
            // Apply opponent's move
            int r1, c1, r2, c2, time;
            iss >> r1 >> c1 >> r2 >> c2 >> time;
            game.updateOpponentAction({r1, c1, r2, c2}, time);
            continue;
        }

        if (command == "FINISH")
        {
            // Game over
            break;
        }

        // Handle unknown command
        cerr << "Invalid command: " << command << endl;
        return 1;
    }

    return 0;
}