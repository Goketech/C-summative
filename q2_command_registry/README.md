# Q2: Network Device Command Registry

Loads approved device commands from a text file into a binary search tree, verifies technician input, suggests close matches, and logs unknown attempts.

## Build and run

```sh
cc -std=c11 -Wall -Wextra -pedantic -o command_registry command_registry.c
./command_registry                    # uses approved_commands.txt
./command_registry other_list.txt     # optional custom command file
```

## Commands

| Command | Operation |
|---|---|
| `v` | Verify a device command |
| `p` | Print all approved commands in alphabetical order (in-order traversal) |
| `s` | Show how many commands are stored |
| `x` | Clear the registry and free every node |
| `h` | Show help |
| `q` | Save a session summary to `registry_session.log` and exit |

## Output files

- `audit.log`: one line per unknown attempt, for example `2026-09-29 15:42:18 | UNKNOWN | RESET_INTERFACES`
- `registry_session.log`: session start and end times, plus approved and unknown counts

## Design notes

- Each node is a single `malloc` holding the links and the command text (flexible array member).
- Insert and search walk the tree iteratively using `strcmp`, so the tree stays in lexicographical order.
- Loading skips blank lines, `#` comments, duplicates, invalid tokens and over-long lines, and stops at 50 commands.
- Only an exact, case-sensitive match is approved. Anything else prints `UNKNOWN COMMAND` and is written to the audit log.
- Suggestions use the edit (Levenshtein) distance, ignoring upper/lower case, against every stored command. A suggestion appears when the distance is within about a quarter of the input length (1 to 3 edits).

## Tested scenarios

- Exact match, near miss (`SHOW_STATU` suggests `SHOW_STATUS`), lowercase input, and unknown input with no suggestion.
- Commands found at different depths of the tree.
- Duplicate commands in the file, an empty file, a missing file, an over-long line, and a 60-command file (capped at 50).
- Searching, printing and clearing an empty tree.
- Invalid input: empty, over 64 characters, and multi-letter menu options.
- Checked with AddressSanitizer: no leaks or invalid memory access.
