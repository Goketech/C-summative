# Q1: Embedded Device Event Logger

Keeps the 25 most recent device events in a dynamically allocated doubly linked list. The operator can browse events in both directions, add events, and save the session.

## Build and run

```sh
cc -std=c11 -Wall -Wextra -pedantic -o event_logger event_logger.c
./event_logger
```

## Commands

| Command | Operation |
|---|---|
| `n` | Move to the next (newer) event |
| `p` | Move to the previous (older) event |
| `a` | Add an event manually (type, sensor/device ID, severity) |
| `g` | Generate simulated sensor events (useful for testing the 25-event cap) |
| `c` | Display the current event |
| `x` | Clear all events |
| `h` | Show help |
| `q` | Save the session to `event_session.txt` and exit |

## Event record

Each node stores an event ID, event type (temperature warning, low battery, sensor failure, communication error), sensor/device ID, timestamp, severity (info, warning, critical), and `prev`/`next` links.

## Design notes

- `EventLogger` tracks `head` (oldest), `tail` (newest), `current`, and the event count.
- New events are appended at the tail, so the list stays in chronological order.
- When the 26th event arrives, `drop_oldest()` unlinks and frees the head before the new event is appended. If `current` pointed at the evicted event, it moves to the new oldest event.
- Navigation only moves `current`; it never relinks nodes.
- `x`, `q` and end of input all release every node. IDs keep increasing across clears, so an ID is never reused.

## Tested scenarios

- Empty list: `n`, `p`, `c` and `x` print messages without crashing.
- The first event inserted becomes `current`.
- Exactly 25 events are kept with no eviction; the 26th evicts the oldest.
- The current event survives eviction when it isn't the oldest one.
- Moving past the oldest or newest event is blocked with a message.
- Adding events after a clear and after the list is full.
- Invalid input: out-of-range numbers, an empty device ID, an over-long ID (truncated), and multi-letter commands.
- Checked with AddressSanitizer: no leaks or invalid memory access.
