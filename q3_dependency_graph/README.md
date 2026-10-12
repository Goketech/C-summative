# Q3: Server Dependency Network Analyzer

Models servers as vertices and dependencies as directed edges. Supports queries in both directions, live updates, and an adjacency-matrix view.

## Build and run

```sh
cc -std=c11 -Wall -Wextra -pedantic -o dependency_graph dependency_graph.c
./dependency_graph
```

## Commands

| Command | Operation |
|---|---|
| `a` | Add a server |
| `r` | Remove a server and all of its dependencies |
| `c` | Create a dependency (from → to) |
| `d` | Delete a dependency |
| `q` | Query a server's dependencies and dependents |
| `m` | Display the adjacency matrix |
| `l` | List servers |
| `h` | Show help |
| `x` | Exit |

## Initial network

```
Web Server            -> API Server, Authentication Server
API Server            -> Database Server, Cache Server
Authentication Server -> Database Server
Cache Server          -> Database Server
Database Server       -> Backup Server
```

## Design notes

- The adjacency matrix is the graph's storage: `links[a][b] = 1` means server `a` depends on server `b`. Because of this, the displayed matrix always matches the current graph.
- A server's dependencies are its row; the servers that depend on it are its column.
- Removing a server shifts the following rows and columns up and left to fill the gap. This drops every edge to or from that server in one step.
- Server names are matched without regard to upper/lower case. The limits are 32 servers and 47-character names.

## Tested scenarios

- Querying with mixed-case names, an unknown server, and an empty graph.
- Duplicate servers, duplicate dependencies, and self-dependencies are rejected.
- Removing a non-existent server or dependency.
- Creating a dependency that involves a server that doesn't exist.
- Removing Database Server drops its 4 dependencies, and the matrix shrinks correctly.
- Removing every server, then running queries and matrix display on the empty graph.
- Empty and over-long names, and the 32-server limit.
