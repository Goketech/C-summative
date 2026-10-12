/*
 * Embedded device event logger.
 * Keeps the most recent MAX_EVENTS events in a doubly linked list,
 * evicting the oldest entry when a new one arrives at capacity.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_EVENTS    25
#define SOURCE_ID_LEN 24
#define INPUT_LEN     64
#define SESSION_FILE  "event_session.txt"

typedef enum {
    KIND_TEMPERATURE_WARNING = 1,
    KIND_LOW_BATTERY,
    KIND_SENSOR_FAILURE,
    KIND_COMM_ERROR
} EventKind;

typedef enum {
    LEVEL_INFO = 1,
    LEVEL_WARNING,
    LEVEL_CRITICAL
} SeverityLevel;

typedef struct DeviceEvent {
    unsigned long id;
    EventKind kind;
    char source[SOURCE_ID_LEN];
    time_t stamp;
    SeverityLevel severity;
    struct DeviceEvent *prev;   /* older */
    struct DeviceEvent *next;   /* newer */
} DeviceEvent;

typedef struct {
    DeviceEvent *head;          /* oldest */
    DeviceEvent *tail;          /* newest */
    DeviceEvent *current;
    int size;
    unsigned long issued_ids;
} EventLogger;

static const char *kind_label(EventKind kind)
{
    switch (kind) {
    case KIND_TEMPERATURE_WARNING: return "TEMPERATURE_WARNING";
    case KIND_LOW_BATTERY:         return "LOW_BATTERY";
    case KIND_SENSOR_FAILURE:      return "SENSOR_FAILURE";
    case KIND_COMM_ERROR:          return "COMM_ERROR";
    }
    return "UNKNOWN";
}

static const char *severity_label(SeverityLevel level)
{
    switch (level) {
    case LEVEL_INFO:     return "INFO";
    case LEVEL_WARNING:  return "WARNING";
    case LEVEL_CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

static void logger_init(EventLogger *logger)
{
    logger->head = NULL;
    logger->tail = NULL;
    logger->current = NULL;
    logger->size = 0;
    logger->issued_ids = 0;
}

static void write_event(FILE *out, const DeviceEvent *evt)
{
    char when[20];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&evt->stamp));
    fprintf(out, "#%-4lu %s | %-19s | %-12s | %s\n",
            evt->id, when, kind_label(evt->kind), evt->source,
            severity_label(evt->severity));
}

static void drop_oldest(EventLogger *logger)
{
    DeviceEvent *victim = logger->head;

    if (victim == NULL)
        return;

    logger->head = victim->next;
    if (logger->head != NULL)
        logger->head->prev = NULL;
    else
        logger->tail = NULL;

    if (logger->current == victim)
        logger->current = logger->head;

    printf("Buffer full: discarded oldest event #%lu\n", victim->id);
    free(victim);
    logger->size--;
}

static void logger_append(EventLogger *logger, DeviceEvent *evt)
{
    if (logger->size >= MAX_EVENTS)
        drop_oldest(logger);

    evt->prev = logger->tail;
    evt->next = NULL;
    if (logger->tail != NULL)
        logger->tail->next = evt;
    else
        logger->head = evt;
    logger->tail = evt;

    if (logger->current == NULL)
        logger->current = evt;

    logger->size++;
}

static int record_event(EventLogger *logger, EventKind kind,
                        const char *source, SeverityLevel severity)
{
    DeviceEvent *evt = malloc(sizeof *evt);

    if (evt == NULL) {
        fputs("Error: out of memory, event not recorded.\n", stderr);
        return 0;
    }

    evt->id = ++logger->issued_ids;
    evt->kind = kind;
    snprintf(evt->source, sizeof evt->source, "%s", source);
    evt->stamp = time(NULL);
    evt->severity = severity;

    logger_append(logger, evt);
    printf("Logged: ");
    write_event(stdout, evt);
    return 1;
}

static void logger_clear(EventLogger *logger)
{
    DeviceEvent *walker = logger->head;
    int released = 0;

    while (walker != NULL) {
        DeviceEvent *following = walker->next;
        free(walker);
        walker = following;
        released++;
    }

    logger->head = NULL;
    logger->tail = NULL;
    logger->current = NULL;
    logger->size = 0;
    printf("Released %d event(s).\n", released);
}

static int current_position(const EventLogger *logger)
{
    int position = 1;
    const DeviceEvent *walker = logger->head;

    while (walker != NULL && walker != logger->current) {
        walker = walker->next;
        position++;
    }
    return position;
}

static void show_current(const EventLogger *logger)
{
    if (logger->current == NULL) {
        puts("No events recorded.");
        return;
    }
    printf("[%d/%d] ", current_position(logger), logger->size);
    write_event(stdout, logger->current);
}

static void save_session(const EventLogger *logger)
{
    FILE *out = fopen(SESSION_FILE, "w");
    time_t now = time(NULL);
    char when[20];
    const DeviceEvent *walker;

    if (out == NULL) {
        printf("Warning: could not write %s; session not saved.\n", SESSION_FILE);
        return;
    }

    strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(out, "Session saved: %s\nEvents retained: %d/%d\n\n",
            when, logger->size, MAX_EVENTS);
    for (walker = logger->head; walker != NULL; walker = walker->next)
        write_event(out, walker);

    fclose(out);
    printf("Session saved to %s (%d event(s)).\n", SESSION_FILE, logger->size);
}

/* Reads one line, dropping the newline and any overflow. Returns 0 on EOF. */
static int read_line(const char *prompt, char *buf, size_t cap)
{
    size_t len;

    if (prompt != NULL) {
        fputs(prompt, stdout);
        fflush(stdout);
    }
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

static int prompt_number(const char *prompt, long low, long high, long *value)
{
    char entry[INPUT_LEN];

    for (;;) {
        char *end;
        long parsed;

        if (!read_line(prompt, entry, sizeof entry))
            return 0;
        strip(entry);
        parsed = strtol(entry, &end, 10);
        if (end != entry && *end == '\0' && parsed >= low && parsed <= high) {
            *value = parsed;
            return 1;
        }
        printf("Enter a whole number from %ld to %ld.\n", low, high);
    }
}

static void add_manual_event(EventLogger *logger)
{
    long kind, level;
    char source[INPUT_LEN];

    puts("Type: 1) Temperature warning  2) Low battery  3) Sensor failure  4) Comm error");
    if (!prompt_number("Event type: ", 1, 4, &kind))
        return;

    for (;;) {
        if (!read_line("Sensor/device ID: ", source, sizeof source))
            return;
        strip(source);
        if (source[0] != '\0')
            break;
        puts("Device ID cannot be empty.");
    }
    if (strlen(source) >= SOURCE_ID_LEN)
        printf("Note: ID truncated to %d characters.\n", SOURCE_ID_LEN - 1);

    puts("Severity: 1) Info  2) Warning  3) Critical");
    if (!prompt_number("Severity: ", 1, 3, &level))
        return;

    record_event(logger, (EventKind)kind, source, (SeverityLevel)level);
}

/* Simulates a burst of hardware-generated events. */
static void generate_events(EventLogger *logger)
{
    static const struct {
        EventKind kind;
        const char *source;
    } feed[] = {
        { KIND_TEMPERATURE_WARNING, "TMP-01"   },
        { KIND_TEMPERATURE_WARNING, "TMP-04"   },
        { KIND_LOW_BATTERY,         "BAT-MAIN" },
        { KIND_SENSOR_FAILURE,      "PRS-07"   },
        { KIND_SENSOR_FAILURE,      "FLOW-02"  },
        { KIND_COMM_ERROR,          "LORA-GW"  },
        { KIND_COMM_ERROR,          "CAN-BUS"  },
    };
    const int feed_size = (int)(sizeof feed / sizeof feed[0]);
    long burst, produced;

    if (!prompt_number("Number of events to generate (1-100): ", 1, 100, &burst))
        return;

    for (produced = 0; produced < burst; produced++) {
        int pick = rand() % feed_size;
        SeverityLevel level = (SeverityLevel)(rand() % 3 + 1);
        if (!record_event(logger, feed[pick].kind, feed[pick].source, level))
            break;
    }
}

static void print_help(void)
{
    puts("Commands:");
    puts("  n  next (newer) event      p  previous (older) event");
    puts("  a  add event manually      g  generate simulated events");
    puts("  c  show current event      x  clear all events");
    puts("  h  help                    q  save session and quit");
}

int main(void)
{
    EventLogger logger;
    char command[INPUT_LEN];
    int running = 1;

    logger_init(&logger);
    srand((unsigned)time(NULL));

    printf("Device Event Logger (capacity: %d events)\n", MAX_EVENTS);
    print_help();

    while (running) {
        if (!read_line("\nlogger> ", command, sizeof command))
            strcpy(command, "q");
        strip(command);

        if (command[0] == '\0')
            continue;
        if (command[1] != '\0') {
            puts("Commands are single letters. Type h for help.");
            continue;
        }

        switch (tolower((unsigned char)command[0])) {
        case 'n':
            if (logger.current == NULL)
                puts("No events recorded.");
            else if (logger.current->next == NULL)
                puts("Already at the newest event.");
            else {
                logger.current = logger.current->next;
                show_current(&logger);
            }
            break;
        case 'p':
            if (logger.current == NULL)
                puts("No events recorded.");
            else if (logger.current->prev == NULL)
                puts("Already at the oldest event.");
            else {
                logger.current = logger.current->prev;
                show_current(&logger);
            }
            break;
        case 'a':
            add_manual_event(&logger);
            break;
        case 'g':
            generate_events(&logger);
            break;
        case 'c':
            show_current(&logger);
            break;
        case 'x':
            logger_clear(&logger);
            break;
        case 'h':
            print_help();
            break;
        case 'q':
            save_session(&logger);
            running = 0;
            break;
        default:
            puts("Unknown command. Type h for help.");
        }
    }

    logger_clear(&logger);
    return 0;
}
