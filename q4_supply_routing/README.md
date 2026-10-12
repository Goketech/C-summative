# Q4: Emergency Medical Supply Routing

Finds the minimum-distance delivery route between two locations in a weighted road network using Dijkstra's algorithm.

## Build and run

```sh
cc -std=c11 -Wall -Wextra -pedantic -o supply_routing supply_routing.c
./supply_routing
```

## Menu

| Option | Operation |
|---|---|
| 1 | Display locations and roads |
| 2 | Find the shortest route (locations can be entered by name or number) |
| 3 | Display the last route's distance, road by road |
| 4 | Add a location |
| 5 | Add or update a road distance |
| 0 | Exit |

## Road network (km)

```
Central Warehouse   -- 8  -- Distribution Center
Central Warehouse   -- 14 -- District Hospital
Distribution Center -- 5  -- Health Center A
Distribution Center -- 11 -- Health Center B
Health Center A     -- 7  -- District Hospital
Health Center B     -- 3  -- Health Center C
Health Center C     -- 6  -- District Hospital
Health Center B     -- 9  -- District Hospital
Health Center A     -- 4  -- Health Center C
```

## Design notes

- The network is an undirected weighted graph stored as a symmetric matrix, where `0` means there is no direct road.
- Dijkstra's algorithm keeps `best_km[]` (shortest distance found so far) and `came_from[]` (the previous stop on that route). The route is rebuilt by following `came_from` back from the destination.
- The search stops early once the destination is settled.
- Every road goes through `set_road()`, which rejects distances that are zero, negative or above 10,000 km, and roads from a location to itself.
- Changing a road clears the last stored route, so option 3 never shows outdated distances.

## Tested scenarios

- Central Warehouse → District Hospital returns the 14 km direct road, not the 20 km route through three roads.
- After that road is changed to 30 km, the route goes Central Warehouse → Distribution Center → Health Center A → District Hospital (20 km).
- Unknown locations, out-of-range numbers, and empty input.
- The same start and destination.
- An unreachable destination (a newly added location with no roads).
- Negative, zero and non-numeric distances.
- Duplicate locations and the 16-location limit.
