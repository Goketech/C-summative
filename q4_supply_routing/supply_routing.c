/*
 * Emergency medical supply routing.
 * Undirected weighted road network; shortest routes found with Dijkstra.
 */
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LOCATIONS 16
#define NAME_LEN      40
#define INPUT_LEN     128
#define MAX_ROAD_KM   10000

typedef struct {
    char names[MAX_LOCATIONS][NAME_LEN];
    int road_km[MAX_LOCATIONS][MAX_LOCATIONS];   /* 0 means no direct road */
    int location_count;
} RoadNetwork;

typedef struct {
    int stops[MAX_LOCATIONS];
    int stop_count;
    int total_km;
    int ready;
} DeliveryRoute;

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

static int add_location(RoadNetwork *net, const char *name)
{
    int idx;

    for (idx = 0; idx < net->location_count; idx++) {
        if (names_match(net->names[idx], name)) {
            printf("Location \"%s\" already exists.\n", name);
            return -1;
        }
    }
    if (net->location_count >= MAX_LOCATIONS) {
        printf("Cannot add location: limit of %d reached.\n", MAX_LOCATIONS);
        return -1;
    }
    snprintf(net->names[net->location_count], NAME_LEN, "%s", name);
    return net->location_count++;
}

static int set_road(RoadNetwork *net, int a, int b, long km)
{
    if (a < 0 || b < 0 || a >= net->location_count || b >= net->location_count) {
        puts("Road rejected: unknown location.");
        return 0;
    }
    if (a == b) {
        puts("Road rejected: both ends are the same location.");
        return 0;
    }
    if (km <= 0 || km > MAX_ROAD_KM) {
        printf("Road rejected: distance must be between 1 and %d km.\n", MAX_ROAD_KM);
        return 0;
    }
    net->road_km[a][b] = (int)km;
    net->road_km[b][a] = (int)km;
    return 1;
}

static void build_default_network(RoadNetwork *net)
{
    static const struct {
        const char *from;
        const char *to;
        int km;
    } roads[] = {
        { "Central Warehouse",   "Distribution Center",  8 },
        { "Central Warehouse",   "District Hospital",   14 },
        { "Distribution Center", "Health Center A",      5 },
        { "Distribution Center", "Health Center B",     11 },
        { "Health Center A",     "District Hospital",    7 },
        { "Health Center B",     "Health Center C",      3 },
        { "Health Center C",     "District Hospital",    6 },
        { "Health Center B",     "District Hospital",    9 },
        { "Health Center A",     "Health Center C",      4 },
    };
    static const char *sites[] = {
        "Central Warehouse", "Distribution Center", "District Hospital",
        "Health Center A", "Health Center B", "Health Center C"
    };
    size_t idx;

    for (idx = 0; idx < sizeof sites / sizeof sites[0]; idx++)
        add_location(net, sites[idx]);

    for (idx = 0; idx < sizeof roads / sizeof roads[0]; idx++) {
        int a = -1, b = -1, scan;
        for (scan = 0; scan < net->location_count; scan++) {
            if (names_match(net->names[scan], roads[idx].from))
                a = scan;
            if (names_match(net->names[scan], roads[idx].to))
                b = scan;
        }
        set_road(net, a, b, roads[idx].km);
    }
}

/* Dijkstra over the adjacency matrix. Returns 1 and fills route if target is reachable. */
static int plan_route(const RoadNetwork *net, int origin, int target, DeliveryRoute *route)
{
    int best_km[MAX_LOCATIONS], came_from[MAX_LOCATIONS], settled[MAX_LOCATIONS];
    int trail[MAX_LOCATIONS];
    int count = net->location_count;
    int idx, round, hops, at;

    for (idx = 0; idx < count; idx++) {
        best_km[idx] = INT_MAX;
        came_from[idx] = -1;
        settled[idx] = 0;
    }
    best_km[origin] = 0;

    for (round = 0; round < count; round++) {
        int nearest = -1, next;

        for (idx = 0; idx < count; idx++) {
            if (!settled[idx] && best_km[idx] != INT_MAX &&
                (nearest < 0 || best_km[idx] < best_km[nearest]))
                nearest = idx;
        }
        if (nearest < 0 || nearest == target)
            break;
        settled[nearest] = 1;

        for (next = 0; next < count; next++) {
            int leg = net->road_km[nearest][next];
            if (leg > 0 && !settled[next] && best_km[nearest] + leg < best_km[next]) {
                best_km[next] = best_km[nearest] + leg;
                came_from[next] = nearest;
            }
        }
    }

    if (best_km[target] == INT_MAX)
        return 0;

    hops = 0;
    for (at = target; at != -1; at = came_from[at])
        trail[hops++] = at;

    for (idx = 0; idx < hops; idx++)
        route->stops[idx] = trail[hops - 1 - idx];
    route->stop_count = hops;
    route->total_km = best_km[target];
    route->ready = 1;
    return 1;
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

/* Accepts either a location number from the list or its name. */
static int read_location(const RoadNetwork *net, const char *prompt)
{
    char entry[INPUT_LEN];
    char *end;
    long number;
    int idx;

    if (!read_line(prompt, entry, sizeof entry))
        return -1;
    if (entry[0] == '\0') {
        puts("No location entered.");
        return -1;
    }

    number = strtol(entry, &end, 10);
    if (end != entry && *end == '\0') {
        if (number >= 1 && number <= net->location_count)
            return (int)number - 1;
        printf("No location numbered %ld.\n", number);
        return -1;
    }

    for (idx = 0; idx < net->location_count; idx++) {
        if (names_match(net->names[idx], entry))
            return idx;
    }
    printf("Location \"%s\" does not exist.\n", entry);
    return -1;
}

static void show_locations(const RoadNetwork *net)
{
    int a, b;

    puts("Locations:");
    for (a = 0; a < net->location_count; a++)
        printf("  %d. %s\n", a + 1, net->names[a]);

    puts("\nRoads:");
    for (a = 0; a < net->location_count; a++) {
        for (b = a + 1; b < net->location_count; b++) {
            if (net->road_km[a][b] > 0)
                printf("  %-20s -- %3d km -- %s\n", net->names[a],
                       net->road_km[a][b], net->names[b]);
        }
    }
}

static void print_route(const RoadNetwork *net, const DeliveryRoute *route)
{
    int idx;

    puts("\nShortest Route:");
    for (idx = 0; idx < route->stop_count; idx++)
        printf("%s%s", idx ? " -> " : "  ", net->names[route->stops[idx]]);
    printf("\n\nTotal Distance: %d km\n", route->total_km);
}

static void find_route(const RoadNetwork *net, DeliveryRoute *last_route)
{
    int origin, target;

    origin = read_location(net, "Start location (name or number): ");
    if (origin < 0)
        return;
    target = read_location(net, "Destination (name or number): ");
    if (target < 0)
        return;

    if (origin == target) {
        puts("Start and destination are the same location; no travel required.");
        return;
    }
    if (!plan_route(net, origin, target, last_route)) {
        printf("%s cannot be reached from %s.\n", net->names[target], net->names[origin]);
        last_route->ready = 0;
        return;
    }
    print_route(net, last_route);
}

static void show_route_distance(const RoadNetwork *net, const DeliveryRoute *route)
{
    int idx;

    if (!route->ready) {
        puts("No route calculated yet. Use option 2 first.");
        return;
    }

    printf("Route: %s to %s\n", net->names[route->stops[0]],
           net->names[route->stops[route->stop_count - 1]]);
    for (idx = 1; idx < route->stop_count; idx++) {
        int from = route->stops[idx - 1], to = route->stops[idx];
        printf("  %-20s -> %-20s %3d km\n", net->names[from], net->names[to],
               net->road_km[from][to]);
    }
    printf("Total Distance: %d km\n", route->total_km);
}

static void add_location_prompt(RoadNetwork *net)
{
    char name[INPUT_LEN];

    if (!read_line("New location name: ", name, sizeof name))
        return;
    if (name[0] == '\0' || strlen(name) >= NAME_LEN) {
        printf("Name must be 1-%d characters.\n", NAME_LEN - 1);
        return;
    }
    if (add_location(net, name) >= 0)
        printf("Location \"%s\" added.\n", name);
}

static void update_road_prompt(RoadNetwork *net, DeliveryRoute *last_route)
{
    char entry[INPUT_LEN];
    char *end;
    long km;
    int a, b;

    if ((a = read_location(net, "First location: ")) < 0)
        return;
    if ((b = read_location(net, "Second location: ")) < 0)
        return;
    if (!read_line("Distance (km): ", entry, sizeof entry))
        return;

    km = strtol(entry, &end, 10);
    if (end == entry || *end != '\0') {
        puts("Invalid distance: enter a whole number of kilometers.");
        return;
    }
    if (set_road(net, a, b, km)) {
        printf("Road %s -- %s set to %ld km.\n", net->names[a], net->names[b], km);
        last_route->ready = 0;
    }
}

static void print_menu(void)
{
    puts("\n1. Display locations");
    puts("2. Find shortest route");
    puts("3. Display route distance");
    puts("4. Add location");
    puts("5. Add or update road");
    puts("0. Exit");
}

int main(void)
{
    static RoadNetwork network;
    DeliveryRoute last_route = { { 0 }, 0, 0, 0 };
    char choice[INPUT_LEN];
    int running = 1;

    build_default_network(&network);
    puts("Emergency Medical Supply Routing");

    while (running) {
        print_menu();
        if (!read_line("Select option: ", choice, sizeof choice))
            break;
        if (strlen(choice) != 1) {
            puts("Invalid option.");
            continue;
        }

        switch (choice[0]) {
        case '1': show_locations(&network); break;
        case '2': find_route(&network, &last_route); break;
        case '3': show_route_distance(&network, &last_route); break;
        case '4': add_location_prompt(&network); break;
        case '5': update_road_prompt(&network, &last_route); break;
        case '0': running = 0; break;
        default:  puts("Invalid option.");
        }
    }

    puts("Goodbye.");
    return 0;
}
