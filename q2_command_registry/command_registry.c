/*
 * Network device command registry.
 * Approved commands are loaded into a BST for lookup; unknown attempts
 * are written to an audit log.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_COMMANDS  50
#define MAX_CMD_LEN   64
#define INPUT_LEN     256
#define COMMAND_FILE  "approved_commands.txt"
#define AUDIT_FILE    "audit.log"
#define SESSION_FILE  "registry_session.log"

typedef struct CommandNode {
    struct CommandNode *left;
    struct CommandNode *right;
    char name[];
} CommandNode;

typedef struct {
    CommandNode *root;
    int count;
} CommandRegistry;

typedef struct {
    time_t started;
    int approved;
    int unknown;
} SessionStats;

typedef enum {
    INSERT_OK,
    INSERT_DUPLICATE,
    INSERT_FULL,
    INSERT_NO_MEMORY
} InsertResult;

static InsertResult registry_insert(CommandRegistry *reg, const char *name)
{
    CommandNode **slot = &reg->root;
    CommandNode *node;
    size_t len;

    while (*slot != NULL) {
        int order = strcmp(name, (*slot)->name);
        if (order == 0)
            return INSERT_DUPLICATE;
        slot = order < 0 ? &(*slot)->left : &(*slot)->right;
    }

    if (reg->count >= MAX_COMMANDS)
        return INSERT_FULL;

    len = strlen(name);
    node = malloc(sizeof *node + len + 1);
    if (node == NULL)
        return INSERT_NO_MEMORY;

    memcpy(node->name, name, len + 1);
    node->left = NULL;
    node->right = NULL;
    *slot = node;
    reg->count++;
    return INSERT_OK;
}

static const CommandNode *registry_find(const CommandRegistry *reg, const char *name)
{
    const CommandNode *walker = reg->root;

    while (walker != NULL) {
        int order = strcmp(name, walker->name);
        if (order == 0)
            return walker;
        walker = order < 0 ? walker->left : walker->right;
    }
    return NULL;
}

static void release_subtree(CommandNode *node)
{
    if (node == NULL)
        return;
    release_subtree(node->left);
    release_subtree(node->right);
    free(node);
}

static void registry_clear(CommandRegistry *reg)
{
    release_subtree(reg->root);
    reg->root = NULL;
    reg->count = 0;
}

static void print_in_order(const CommandNode *node, int *index)
{
    if (node == NULL)
        return;
    print_in_order(node->left, index);
    printf("  %2d. %s\n", ++(*index), node->name);
    print_in_order(node->right, index);
}

/* Case-insensitive Levenshtein distance; both inputs are at most MAX_CMD_LEN. */
static int edit_distance(const char *first, const char *second)
{
    int above[MAX_CMD_LEN + 1], row[MAX_CMD_LEN + 1];
    size_t first_len = strlen(first), second_len = strlen(second);
    size_t i, j;

    for (j = 0; j <= second_len; j++)
        above[j] = (int)j;

    for (i = 1; i <= first_len; i++) {
        row[0] = (int)i;
        for (j = 1; j <= second_len; j++) {
            int same = toupper((unsigned char)first[i - 1]) ==
                       toupper((unsigned char)second[j - 1]);
            int best = above[j - 1] + (same ? 0 : 1);
            if (above[j] + 1 < best)
                best = above[j] + 1;
            if (row[j - 1] + 1 < best)
                best = row[j - 1] + 1;
            row[j] = best;
        }
        memcpy(above, row, (second_len + 1) * sizeof row[0]);
    }
    return above[second_len];
}

static void closest_match(const CommandNode *node, const char *attempt,
                          const CommandNode **best, int *best_distance)
{
    int distance;

    if (node == NULL)
        return;
    closest_match(node->left, attempt, best, best_distance);
    distance = edit_distance(attempt, node->name);
    if (distance < *best_distance) {
        *best_distance = distance;
        *best = node;
    }
    closest_match(node->right, attempt, best, best_distance);
}

static int suggestion_tolerance(const char *attempt)
{
    int allowed = (int)strlen(attempt) / 4;
    if (allowed < 1)
        allowed = 1;
    if (allowed > 3)
        allowed = 3;
    return allowed;
}

static void format_now(char *buf, size_t cap)
{
    time_t now = time(NULL);
    strftime(buf, cap, "%Y-%m-%d %H:%M:%S", localtime(&now));
}

static void audit_unknown(const char *attempt)
{
    FILE *audit = fopen(AUDIT_FILE, "a");
    char when[20];

    if (audit == NULL) {
        printf("Warning: could not open %s for audit logging.\n", AUDIT_FILE);
        return;
    }
    format_now(when, sizeof when);
    fprintf(audit, "%s | UNKNOWN | %s\n", when, attempt);
    fclose(audit);
}

static void strip(char *text)
{
    char *start = text;
    size_t len;

    while (isspace((unsigned char)*start))
        start++;
    len = strlen(start);
    while (len > 0 && isspace((unsigned char)start[len - 1]))
        len--;
    memmove(text, start, len);
    text[len] = '\0';
}

static int is_command_token(const char *text)
{
    size_t len = strlen(text);

    if (len == 0 || len > MAX_CMD_LEN)
        return 0;
    for (; *text != '\0'; text++) {
        if (!isalnum((unsigned char)*text) && *text != '_' && *text != '-')
            return 0;
    }
    return 1;
}

static void registry_load(CommandRegistry *reg, const char *path)
{
    FILE *src = fopen(path, "r");
    char line[INPUT_LEN];
    int line_no = 0, duplicates = 0, rejected = 0;

    if (src == NULL) {
        printf("Warning: %s not found. Registry is empty.\n", path);
        return;
    }

    while (fgets(line, sizeof line, src) != NULL) {
        InsertResult result;

        line_no++;
        if (strchr(line, '\n') == NULL && !feof(src)) {
            int ch;
            while ((ch = fgetc(src)) != '\n' && ch != EOF)
                ;
            printf("  line %d: entry too long, skipped\n", line_no);
            rejected++;
            continue;
        }

        strip(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (!is_command_token(line)) {
            printf("  line %d: invalid command \"%s\", skipped\n", line_no, line);
            rejected++;
            continue;
        }

        result = registry_insert(reg, line);
        if (result == INSERT_DUPLICATE) {
            printf("  line %d: duplicate \"%s\" ignored\n", line_no, line);
            duplicates++;
        } else if (result == INSERT_FULL) {
            printf("  Limit of %d commands reached; remaining entries ignored from line %d.\n",
                   MAX_COMMANDS, line_no);
            break;
        } else if (result == INSERT_NO_MEMORY) {
            puts("  Out of memory; loading stopped.");
            break;
        }
    }
    fclose(src);

    if (reg->count == 0)
        printf("Warning: %s contains no approved commands.\n", path);
    printf("Loaded %d command(s) from %s (%d duplicate, %d rejected).\n",
           reg->count, path, duplicates, rejected);
}

static int read_line(const char *prompt, char *buf, size_t cap)
{
    size_t len;

    fputs(prompt, stdout);
    fflush(stdout);
    if (fgets(buf, (int)cap, stdin) == NULL)
        return 0;

    len = strcspn(buf, "\n");
    if (buf[len] == '\n') {
        buf[len] = '\0';
    } else {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
            ;
    }
    return 1;
}

static void verify_command(const CommandRegistry *reg, SessionStats *stats)
{
    char attempt[INPUT_LEN];
    const CommandNode *nearest = NULL;
    int nearest_distance = MAX_CMD_LEN + 1;

    if (!read_line("Enter device command: ", attempt, sizeof attempt))
        return;
    strip(attempt);

    if (attempt[0] == '\0') {
        puts("No command entered.");
        return;
    }
    if (strlen(attempt) > MAX_CMD_LEN) {
        printf("Invalid input: commands are at most %d characters.\n", MAX_CMD_LEN);
        return;
    }

    if (registry_find(reg, attempt) != NULL) {
        puts("COMMAND APPROVED");
        stats->approved++;
        return;
    }

    puts("UNKNOWN COMMAND");
    stats->unknown++;
    audit_unknown(attempt);

    closest_match(reg->root, attempt, &nearest, &nearest_distance);
    if (nearest != NULL && nearest_distance <= suggestion_tolerance(attempt))
        printf("Did you mean: %s?\n", nearest->name);
}

static void save_session(const CommandRegistry *reg, const SessionStats *stats)
{
    FILE *out = fopen(SESSION_FILE, "a");
    char started[20], ended[20];

    if (out == NULL) {
        printf("Warning: could not write %s; session not saved.\n", SESSION_FILE);
        return;
    }
    strftime(started, sizeof started, "%Y-%m-%d %H:%M:%S", localtime(&stats->started));
    format_now(ended, sizeof ended);
    fprintf(out, "%s -> %s | commands=%d approved=%d unknown=%d\n",
            started, ended, reg->count, stats->approved, stats->unknown);
    fclose(out);
    printf("Session saved to %s.\n", SESSION_FILE);
}

static void print_help(void)
{
    puts("Commands:");
    puts("  v  verify a device command");
    puts("  p  print approved commands (alphabetical)");
    puts("  s  show number of stored commands");
    puts("  x  clear the registry");
    puts("  h  help");
    puts("  q  save session and quit");
}

int main(int argc, char *argv[])
{
    CommandRegistry registry = { NULL, 0 };
    SessionStats stats = { 0, 0, 0 };
    const char *source = argc > 1 ? argv[1] : COMMAND_FILE;
    char choice[INPUT_LEN];
    int running = 1;

    stats.started = time(NULL);
    puts("Network Device Command Registry");
    registry_load(&registry, source);
    print_help();

    while (running) {
        if (!read_line("\nregistry> ", choice, sizeof choice))
            strcpy(choice, "q");
        strip(choice);

        if (choice[0] == '\0')
            continue;
        if (choice[1] != '\0') {
            puts("Invalid option. Type h for help.");
            continue;
        }

        switch (tolower((unsigned char)choice[0])) {
        case 'v':
            verify_command(&registry, &stats);
            break;
        case 'p':
            if (registry.root == NULL) {
                puts("Registry is empty.");
            } else {
                int index = 0;
                puts("Approved commands:");
                print_in_order(registry.root, &index);
            }
            break;
        case 's':
            printf("Stored commands: %d/%d\n", registry.count, MAX_COMMANDS);
            break;
        case 'x': {
            int released = registry.count;
            registry_clear(&registry);
            printf("Registry cleared (%d command(s) released).\n", released);
            break;
        }
        case 'h':
            print_help();
            break;
        case 'q':
            save_session(&registry, &stats);
            running = 0;
            break;
        default:
            puts("Invalid option. Type h for help.");
        }
    }

    registry_clear(&registry);
    return 0;
}
