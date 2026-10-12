/*
 * Server dependency network analyzer.
 * Directed graph stored as an adjacency matrix: links[a][b] = 1 means
 * server a depends on (communicates with) server b.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define MAX_SERVERS 32
#define NAME_LEN    48
#define INPUT_LEN   128

typedef struct {
    char names[MAX_SERVERS][NAME_LEN];
    unsigned char links[MAX_SERVERS][MAX_SERVERS];
    int server_count;
} ServerGraph;

static int names_match(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        left++;
        right++;
    }
    return *left == *right;
}

static int find_server(const ServerGraph *graph, const char *name)
{
    int idx;

    for (idx = 0; idx < graph->server_count; idx++) {
        if (names_match(graph->names[idx], name))
            return idx;
    }
    return -1;
}

static int add_server(ServerGraph *graph, const char *name)
{
    if (find_server(graph, name) >= 0) {
        printf("Server \"%s\" already exists.\n", name);
        return 0;
    }
    if (graph->server_count >= MAX_SERVERS) {
        printf("Cannot add server: limit of %d reached.\n", MAX_SERVERS);
        return 0;
    }

    snprintf(graph->names[graph->server_count], NAME_LEN, "%s", name);
    graph->server_count++;
    return 1;
}

/* Removes the vertex and closes the gap in both the name list and the matrix. */
static int remove_server(ServerGraph *graph, const char *name)
{
    int target = find_server(graph, name);
    int last, row, col, dropped_links = 0;

    if (target < 0) {
        printf("Server \"%s\" not found.\n", name);
        return 0;
    }

    last = graph->server_count - 1;
    for (col = 0; col <= last; col++)
        dropped_links += graph->links[target][col] + graph->links[col][target];
    dropped_links -= graph->links[target][target];

    printf("Removed \"%s\" and %d related dependenc%s.\n",
           graph->names[target], dropped_links, dropped_links == 1 ? "y" : "ies");

    for (row = target; row < last; row++) {
        memcpy(graph->names[row], graph->names[row + 1], NAME_LEN);
        memcpy(graph->links[row], graph->links[row + 1], sizeof graph->links[row]);
    }
    for (row = 0; row < last; row++)
        memmove(&graph->links[row][target], &graph->links[row][target + 1],
                (size_t)(last - target));

    for (col = 0; col <= last; col++) {
        graph->links[last][col] = 0;
        graph->links[col][last] = 0;
    }
    graph->names[last][0] = '\0';
    graph->server_count--;
    return 1;
}

static int set_dependency(ServerGraph *graph, const char *from, const char *to, int enable)
{
    int src = find_server(graph, from);
    int dst = find_server(graph, to);

    if (src < 0 || dst < 0) {
        printf("Unknown server: \"%s\".\n", src < 0 ? from : to);
        return 0;
    }
    if (src == dst) {
        puts("A server cannot depend on itself.");
        return 0;
    }

    if (enable && graph->links[src][dst]) {
        printf("Dependency %s -> %s already exists.\n", graph->names[src], graph->names[dst]);
        return 0;
    }
    if (!enable && !graph->links[src][dst]) {
        printf("No dependency %s -> %s to remove.\n", graph->names[src], graph->names[dst]);
        return 0;
    }

    graph->links[src][dst] = (unsigned char)(enable ? 1 : 0);
    printf("Dependency %s -> %s %s.\n", graph->names[src], graph->names[dst],
           enable ? "created" : "removed");
    return 1;
}

static void query_server(const ServerGraph *graph, const char *name)
{
    int target = find_server(graph, name);
    int other, listed;

    if (target < 0) {
        printf("Server \"%s\" not found.\n", name);
        return;
    }

    printf("\nServer: %s\n\nDirect dependencies:\n", graph->names[target]);
    for (other = 0, listed = 0; other < graph->server_count; other++) {
        if (graph->links[target][other]) {
            printf("  %s\n", graph->names[other]);
            listed++;
        }
    }
    if (listed == 0)
        puts("  (none)");

    printf("\nServices depending on %s:\n", graph->names[target]);
    for (other = 0, listed = 0; other < graph->server_count; other++) {
        if (graph->links[other][target]) {
            printf("  %s\n", graph->names[other]);
            listed++;
        }
    }
    if (listed == 0)
        puts("  (none)");
}

static void list_servers(const ServerGraph *graph)
{
    int idx;

    for (idx = 0; idx < graph->server_count; idx++)
        printf("  [%d] %s\n", idx, graph->names[idx]);
}

static void show_matrix(const ServerGraph *graph)
{
    int row, col;

    if (graph->server_count == 0) {
        puts("The network has no servers.");
        return;
    }

    puts("Legend:");
    list_servers(graph);

    printf("\n     ");
    for (col = 0; col < graph->server_count; col++)
        printf("%3d", col);
    putchar('\n');

    for (row = 0; row < graph->server_count; row++) {
        printf("%3d |", row);
        for (col = 0; col < graph->server_count; col++)
            printf("%3d", graph->links[row][col]);
        putchar('\n');
    }
}

static void seed_network(ServerGraph *graph)
{
    static const char *servers[] = {
        "Web Server", "API Server", "Authentication Server",
        "Database Server", "Cache Server", "Backup Server"
    };
    static const char *edges[][2] = {
        { "Web Server",            "API Server"            },
        { "Web Server",            "Authentication Server" },
        { "API Server",            "Database Server"       },
        { "API Server",            "Cache Server"          },
        { "Authentication Server", "Database Server"       },
        { "Cache Server",          "Database Server"       },
        { "Database Server",       "Backup Server"         },
    };
    size_t idx;

    for (idx = 0; idx < sizeof servers / sizeof servers[0]; idx++)
        add_server(graph, servers[idx]);
    for (idx = 0; idx < sizeof edges / sizeof edges[0]; idx++)
        graph->links[find_server(graph, edges[idx][0])][find_server(graph, edges[idx][1])] = 1;
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
    strip(buf);
    return 1;
}

/* Prompts for a server name; returns 0 on EOF or invalid input. */
static int read_server_name(const char *prompt, char *buf, size_t cap)
{
    if (!read_line(prompt, buf, cap))
        return 0;
    if (buf[0] == '\0') {
        puts("Server name cannot be empty.");
        return 0;
    }
    if (strlen(buf) >= NAME_LEN) {
        printf("Server name must be under %d characters.\n", NAME_LEN);
        return 0;
    }
    return 1;
}

static int graph_has_servers(const ServerGraph *graph)
{
    if (graph->server_count == 0) {
        puts("The network has no servers.");
        return 0;
    }
    return 1;
}

static void print_help(void)
{
    puts("Commands:");
    puts("  a  add server            r  remove server");
    puts("  c  create dependency     d  delete dependency");
    puts("  q  query server          m  show adjacency matrix");
    puts("  l  list servers          h  help");
    puts("  x  exit");
}

int main(void)
{
    static ServerGraph graph;
    char choice[INPUT_LEN], first[INPUT_LEN], second[INPUT_LEN];
    int running = 1;

    seed_network(&graph);
    puts("Server Dependency Network Analyzer");
    print_help();

    while (running) {
        if (!read_line("\nnetwork> ", choice, sizeof choice))
            break;
        if (choice[0] == '\0')
            continue;
        if (choice[1] != '\0') {
            puts("Invalid option. Type h for help.");
            continue;
        }

        switch (tolower((unsigned char)choice[0])) {
        case 'a':
            if (read_server_name("New server name: ", first, sizeof first) &&
                add_server(&graph, first))
                printf("Server \"%s\" added.\n", first);
            break;
        case 'r':
            if (graph_has_servers(&graph) &&
                read_server_name("Server to remove: ", first, sizeof first))
                remove_server(&graph, first);
            break;
        case 'c':
        case 'd':
            if (graph_has_servers(&graph) &&
                read_server_name("From server: ", first, sizeof first) &&
                read_server_name("To server: ", second, sizeof second))
                set_dependency(&graph, first, second, tolower((unsigned char)choice[0]) == 'c');
            break;
        case 'q':
            if (graph_has_servers(&graph) &&
                read_server_name("Server to query: ", first, sizeof first))
                query_server(&graph, first);
            break;
        case 'm':
            show_matrix(&graph);
            break;
        case 'l':
            if (graph_has_servers(&graph))
                list_servers(&graph);
            break;
        case 'h':
            print_help();
            break;
        case 'x':
            running = 0;
            break;
        default:
            puts("Invalid option. Type h for help.");
        }
    }

    puts("Goodbye.");
    return 0;
}
