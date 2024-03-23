# Workers System Simulation
Workers System Simulation is a simple program simulating a task distribution system among worker processes using POSIX message queues.

## Building and Running
To compile the program, use the provided `Makefile`:
```bash
make
```
This will generate an executable `workers`.

To run the program, use following command:
```bash
./workers N T1 T2
```
Where:
- N is the number of workers (2 <= N <= 20)
- T1 and T2 are the minimum and maximum delay in ms between sending tasks by the server (100 <= T1 < T2 <= 5000)

For example:
```bash
./workers 5 200 500
```
This command will start the program with 5 workers and the server will be sending tasks between 200 ms and 500 ms.

## Program description
- The main process (server) adds new tasks to the queue at random intervals (between T1 and T2 milliseconds) by generating random pairs of floating-point numbers in the range of 0.0 to 100.0.
- Initially, N child processes (workers) are created (where 2 <= N <= 20), which register themselves in the task queue named `task_queue_{server_pid}`.
- Workers wait for tasks, retrieve them from the queue when available and they are not busy. Each worker returns results through its own queue named `result_queue_{server_pid}_{worker_pid}`.
- The main process continues creating tasks until it receives a `SIGINT` signal, at which point it informs workers of the end of work (via the queue).
- The server waits for the current tasks of the workers to finish, then terminates. Workers finish their work upon receiving the termination message from the server (only finishing started tasks - tasks in the queue are ignored).
