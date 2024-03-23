# Roulette Simulation
Roulette Simulation is a simple program simulating a roulette game using POSIX pipes.
	
## Building and Running
To compile the program, use the provided `Makefile`:
```bash
make
```
This will generate an executable `roulette`.

To run the program, use following command:
```bash
./roulette N M
```
Where:
- N is the number of players (N >= 1)
- M is the initial amount of money each player has (M >= 100)

For example:
```bash
./roulette 5 500
```
This command will start the game with 5 players, each starting with $500.

## Program description
- The main process (croupier) creates N child processes (players).
- Players communicate with the Croupier using POSIX pipes.
- In every round each player randomly selects an amount to bet (within their budget) and a number to bet on (0 to 36) with a payout of 35:1.
- The croupier announces who bet how much and on which number.
- The game continues as long as at least one player has money.
- If a player runs out of money, a message is printed, and the player leaves the game.
- Upon winning, a player prints the amount won.
- After all players leave, the dealer prints a message indicating the end of the game and exits.
- During each round, each player has a 10% chance of quitting the game. If a player quits, they print the amount saved and exit.
