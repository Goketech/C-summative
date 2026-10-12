# C Summative

Five standalone C programs, one per folder. Each folder has its own README with build steps, commands and design notes.

| Folder | Problem | Core data structure |
|---|---|---|
| [`q1_event_logger`](q1_event_logger) | Embedded device event logger | Doubly linked list (25-event cap) |
| [`q2_command_registry`](q2_command_registry) | Network device command registry | Binary search tree (50-command cap) |
| [`q3_dependency_graph`](q3_dependency_graph) | Server dependency analyzer | Directed graph, adjacency matrix |
| [`q4_supply_routing`](q4_supply_routing) | Medical supply routing | Weighted graph + Dijkstra |
| [`q5_log_compression`](q5_log_compression) | Firmware log compression | Huffman coding |

## Build

Each program is a single C11 file with no external dependencies:

```sh
cd q1_event_logger
cc -std=c11 -Wall -Wextra -pedantic -o event_logger event_logger.c
./event_logger
```

Run each program from inside its folder so it finds its data files.
