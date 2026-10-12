/*
 * Huffman compression utility for embedded diagnostic logs.
 *
 * .huff layout:
 *   "HUF1" | original size (u64, little-endian) | bitstream
 * The bitstream starts with the tree in pre-order (0 = internal node,
 * 1 + 8 bits = leaf symbol), followed by the encoded data.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ORIGINAL_PATH "device_log.txt"
#define PACKED_PATH   "device_log.huff"
#define RESTORED_PATH "device_log_restored.txt"
#define SYMBOL_RANGE  256
#define INPUT_LEN     64

static const unsigned char FILE_TAG[4] = { 'H', 'U', 'F', '1' };

typedef struct TreeNode {
    unsigned long long weight;
    int rank;                   /* tie-breaker for equal weights, keeps output deterministic */
    int symbol;                 /* -1 for internal nodes */
    struct TreeNode *zero;
    struct TreeNode *one;
} TreeNode;

typedef struct {
    TreeNode *slots[SYMBOL_RANGE];
    int size;
} MinHeap;

typedef struct {
    FILE *out;
    unsigned char pending;
    int used;
} BitWriter;

typedef struct {
    FILE *in;
    int current;
    int remaining;
} BitReader;

static char code_table[SYMBOL_RANGE][SYMBOL_RANGE];

static TreeNode *make_node(unsigned long long weight, int rank, int symbol,
                           TreeNode *zero, TreeNode *one)
{
    TreeNode *node = malloc(sizeof *node);

    if (node == NULL)
        return NULL;
    node->weight = weight;
    node->rank = rank;
    node->symbol = symbol;
    node->zero = zero;
    node->one = one;
    return node;
}

static void free_tree(TreeNode *node)
{
    if (node == NULL)
        return;
    free_tree(node->zero);
    free_tree(node->one);
    free(node);
}

static int lighter(const TreeNode *a, const TreeNode *b)
{
    if (a->weight != b->weight)
        return a->weight < b->weight;
    return a->rank < b->rank;
}

static void heap_push(MinHeap *heap, TreeNode *node)
{
    int child = heap->size++;

    while (child > 0) {
        int parent = (child - 1) / 2;
        if (!lighter(node, heap->slots[parent]))
            break;
        heap->slots[child] = heap->slots[parent];
        child = parent;
    }
    heap->slots[child] = node;
}

static TreeNode *heap_pop(MinHeap *heap)
{
    TreeNode *top = heap->slots[0];
    TreeNode *last = heap->slots[--heap->size];
    int hole = 0;

    for (;;) {
        int pick = 2 * hole + 1;
        if (pick >= heap->size)
            break;
        if (pick + 1 < heap->size && lighter(heap->slots[pick + 1], heap->slots[pick]))
            pick++;
        if (!lighter(heap->slots[pick], last))
            break;
        heap->slots[hole] = heap->slots[pick];
        hole = pick;
    }
    heap->slots[hole] = last;
    return top;
}

/* Returns NULL when no symbols are present or memory runs out. */
static TreeNode *build_tree(const unsigned long long freq[SYMBOL_RANGE])
{
    MinHeap heap;
    int sym, merges = 0;

    heap.size = 0;
    for (sym = 0; sym < SYMBOL_RANGE; sym++) {
        if (freq[sym] > 0) {
            TreeNode *leaf = make_node(freq[sym], sym, sym, NULL, NULL);
            if (leaf == NULL)
                goto out_of_memory;
            heap_push(&heap, leaf);
        }
    }
    if (heap.size == 0)
        return NULL;

    while (heap.size > 1) {
        TreeNode *low = heap_pop(&heap);
        TreeNode *high = heap_pop(&heap);
        TreeNode *parent = make_node(low->weight + high->weight,
                                     SYMBOL_RANGE + merges++, -1, low, high);
        if (parent == NULL) {
            free_tree(low);
            free_tree(high);
            goto out_of_memory;
        }
        heap_push(&heap, parent);
    }
    return heap_pop(&heap);

out_of_memory:
    fputs("Error: out of memory while building Huffman tree.\n", stderr);
    while (heap.size > 0)
        free_tree(heap_pop(&heap));
    return NULL;
}

static void assign_codes(const TreeNode *node, char *path, int depth)
{
    if (node->symbol >= 0) {
        if (depth == 0)
            path[depth++] = '0';    /* single-symbol input still needs one bit */
        memcpy(code_table[node->symbol], path, (size_t)depth);
        code_table[node->symbol][depth] = '\0';
        return;
    }
    path[depth] = '0';
    assign_codes(node->zero, path, depth + 1);
    path[depth] = '1';
    assign_codes(node->one, path, depth + 1);
}

static unsigned char *load_file(const char *path, size_t *length)
{
    FILE *src = fopen(path, "rb");
    unsigned char *data = NULL;
    size_t used = 0, capacity = 0;

    if (src == NULL) {
        printf("Error: cannot open %s.\n", path);
        return NULL;
    }

    for (;;) {
        size_t got;

        if (used == capacity) {
            size_t grown = capacity ? capacity * 2 : 4096;
            unsigned char *bigger = realloc(data, grown);
            if (bigger == NULL) {
                puts("Error: out of memory while reading file.");
                free(data);
                fclose(src);
                return NULL;
            }
            data = bigger;
            capacity = grown;
        }
        got = fread(data + used, 1, capacity - used, src);
        used += got;
        if (got == 0)
            break;
    }

    if (ferror(src)) {
        printf("Error: failed while reading %s.\n", path);
        free(data);
        fclose(src);
        return NULL;
    }
    fclose(src);
    *length = used;
    return data;
}

static void put_le(FILE *out, unsigned long long value, int width)
{
    int shift;
    for (shift = 0; shift < width; shift++)
        fputc((int)((value >> (8 * shift)) & 0xFF), out);
}

static int get_le(FILE *in, int width, unsigned long long *value)
{
    int shift;

    *value = 0;
    for (shift = 0; shift < width; shift++) {
        int byte = fgetc(in);
        if (byte == EOF)
            return 0;
        *value |= (unsigned long long)byte << (8 * shift);
    }
    return 1;
}

static void write_bit(BitWriter *writer, int bit)
{
    writer->pending = (unsigned char)((writer->pending << 1) | (bit & 1));
    if (++writer->used == 8) {
        fputc(writer->pending, writer->out);
        writer->pending = 0;
        writer->used = 0;
    }
}

static void flush_bits(BitWriter *writer)
{
    if (writer->used > 0) {
        fputc((unsigned char)(writer->pending << (8 - writer->used)), writer->out);
        writer->pending = 0;
        writer->used = 0;
    }
}

static void write_byte_bits(BitWriter *writer, int value)
{
    int shift;
    for (shift = 7; shift >= 0; shift--)
        write_bit(writer, (value >> shift) & 1);
}

static void write_tree(BitWriter *writer, const TreeNode *node)
{
    if (node->symbol >= 0) {
        write_bit(writer, 1);
        write_byte_bits(writer, node->symbol);
        return;
    }
    write_bit(writer, 0);
    write_tree(writer, node->zero);
    write_tree(writer, node->one);
}

/* Returns the next bit, or -1 at end of file. */
static int read_bit(BitReader *reader)
{
    if (reader->remaining == 0) {
        reader->current = fgetc(reader->in);
        if (reader->current == EOF)
            return -1;
        reader->remaining = 8;
    }
    reader->remaining--;
    return (reader->current >> reader->remaining) & 1;
}

static TreeNode *read_tree(BitReader *reader, int depth)
{
    TreeNode *zero, *one, *node;
    int flag = read_bit(reader);

    if (flag < 0 || depth >= SYMBOL_RANGE)
        return NULL;

    if (flag == 1) {
        int symbol = 0, i;
        for (i = 0; i < 8; i++) {
            int bit = read_bit(reader);
            if (bit < 0)
                return NULL;
            symbol = (symbol << 1) | bit;
        }
        return make_node(0, 0, symbol, NULL, NULL);
    }

    zero = read_tree(reader, depth + 1);
    one = zero != NULL ? read_tree(reader, depth + 1) : NULL;
    node = one != NULL ? make_node(0, 0, -1, zero, one) : NULL;
    if (node == NULL) {
        free_tree(zero);
        free_tree(one);
    }
    return node;
}

static void describe_symbol(int sym, char *buf, size_t cap)
{
    switch (sym) {
    case '\n': snprintf(buf, cap, "'\\n'"); return;
    case '\r': snprintf(buf, cap, "'\\r'"); return;
    case '\t': snprintf(buf, cap, "'\\t'"); return;
    case ' ':  snprintf(buf, cap, "' '");   return;
    }
    if (isprint(sym))
        snprintf(buf, cap, "'%c'", sym);
    else
        snprintf(buf, cap, "0x%02X", sym);
}

static void print_symbol_table(const unsigned long long freq[SYMBOL_RANGE])
{
    int order[SYMBOL_RANGE];
    int present = 0, sym, i;
    char label[16];

    for (sym = 0; sym < SYMBOL_RANGE; sym++) {
        if (freq[sym] == 0)
            continue;
        for (i = present; i > 0 && freq[order[i - 1]] < freq[sym]; i--)
            order[i] = order[i - 1];
        order[i] = sym;
        present++;
    }

    printf("\n%-10s %-10s %s\n", "Character", "Frequency", "Code");
    for (i = 0; i < present; i++) {
        describe_symbol(order[i], label, sizeof label);
        printf("%-10s %-10llu %s\n", label, freq[order[i]], code_table[order[i]]);
    }
    printf("Distinct characters: %d\n", present);
}

static void compress_log(void)
{
    unsigned long long freq[SYMBOL_RANGE] = { 0 };
    unsigned char *content;
    size_t length, pos;
    TreeNode *root = NULL;
    FILE *out;
    BitWriter writer;
    long packed_size;
    int write_failed;

    content = load_file(ORIGINAL_PATH, &length);
    if (content == NULL)
        return;

    for (pos = 0; pos < length; pos++)
        freq[content[pos]]++;

    memset(code_table, 0, sizeof code_table);
    if (length > 0) {
        char path[SYMBOL_RANGE];
        root = build_tree(freq);
        if (root == NULL) {
            free(content);
            return;
        }
        assign_codes(root, path, 0);
    }
    print_symbol_table(freq);

    out = fopen(PACKED_PATH, "wb");
    if (out == NULL) {
        printf("Error: cannot create %s.\n", PACKED_PATH);
        free_tree(root);
        free(content);
        return;
    }

    fwrite(FILE_TAG, 1, sizeof FILE_TAG, out);
    put_le(out, (unsigned long long)length, 8);

    writer.out = out;
    writer.pending = 0;
    writer.used = 0;
    if (root != NULL)
        write_tree(&writer, root);
    for (pos = 0; pos < length; pos++) {
        const char *bits;
        for (bits = code_table[content[pos]]; *bits != '\0'; bits++)
            write_bit(&writer, *bits == '1');
    }
    flush_bits(&writer);

    packed_size = ftell(out);
    write_failed = ferror(out);
    if (fclose(out) != 0)
        write_failed = 1;

    if (write_failed) {
        printf("Error: failed while writing %s.\n", PACKED_PATH);
    } else {
        printf("\nSaved %s\n", PACKED_PATH);
        printf("Original size:     %zu bytes\n", length);
        printf("Compressed size:   %ld bytes\n", packed_size);
        if (length > 0)
            printf("Compression ratio: %.2f%% (space saved: %.2f%%)\n",
                   100.0 * (double)packed_size / (double)length,
                   100.0 - 100.0 * (double)packed_size / (double)length);
    }

    free_tree(root);
    free(content);
}

static int verify_restored(void)
{
    FILE *original = fopen(ORIGINAL_PATH, "rb");
    FILE *restored = fopen(RESTORED_PATH, "rb");
    unsigned char left[4096], right[4096];
    unsigned long long offset = 0;
    int identical = 1;

    if (original == NULL || restored == NULL) {
        printf("Cannot verify: %s is missing.\n",
               original == NULL ? ORIGINAL_PATH : RESTORED_PATH);
        if (original != NULL)
            fclose(original);
        if (restored != NULL)
            fclose(restored);
        return 0;
    }

    for (;;) {
        size_t got_left = fread(left, 1, sizeof left, original);
        size_t got_right = fread(right, 1, sizeof right, restored);
        size_t common = got_left < got_right ? got_left : got_right;
        size_t i;

        for (i = 0; i < common && left[i] == right[i]; i++)
            ;
        if (i < common || got_left != got_right) {
            offset += i;
            identical = 0;
            break;
        }
        offset += common;
        if (got_left == 0)
            break;
    }
    fclose(original);
    fclose(restored);

    if (identical) {
        puts("Verification: SUCCESS");
        puts("The restored file is identical to the original.");
    } else {
        puts("Verification: FAILED");
        printf("The restored file differs from the original (first difference at byte %llu).\n",
               offset);
    }
    return identical;
}

static void decompress_log(void)
{
    unsigned long long original_size, produced = 0;
    unsigned char tag[sizeof FILE_TAG];
    TreeNode *root = NULL;
    const TreeNode *walker;
    BitReader reader;
    FILE *in, *out;

    in = fopen(PACKED_PATH, "rb");
    if (in == NULL) {
        printf("Error: cannot open %s. Compress the log first.\n", PACKED_PATH);
        return;
    }

    if (fread(tag, 1, sizeof tag, in) != sizeof tag || memcmp(tag, FILE_TAG, sizeof tag) != 0 ||
        !get_le(in, 8, &original_size)) {
        printf("Error: %s is not a valid compressed log.\n", PACKED_PATH);
        fclose(in);
        return;
    }

    reader.in = in;
    reader.current = 0;
    reader.remaining = 0;
    if (original_size > 0 && (root = read_tree(&reader, 0)) == NULL) {
        printf("Error: corrupt Huffman tree in %s.\n", PACKED_PATH);
        fclose(in);
        return;
    }

    out = fopen(RESTORED_PATH, "wb");
    if (out == NULL) {
        printf("Error: cannot create %s.\n", RESTORED_PATH);
        free_tree(root);
        fclose(in);
        return;
    }

    walker = root;
    while (produced < original_size) {
        int bit = read_bit(&reader);
        if (bit < 0)
            break;
        if (root->symbol < 0)
            walker = bit ? walker->one : walker->zero;
        if (walker->symbol >= 0) {
            fputc(walker->symbol, out);
            produced++;
            walker = root;
        }
    }
    fclose(in);
    free_tree(root);

    if (fclose(out) != 0 || produced != original_size) {
        printf("Error: %s is truncated; restored %llu of %llu bytes.\n",
               PACKED_PATH, produced, original_size);
        return;
    }

    printf("Restored %llu bytes to %s\n\n", produced, RESTORED_PATH);
    verify_restored();
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

int main(void)
{
    char choice[INPUT_LEN];
    int running = 1;

    puts("Firmware Log Compression Utility");

    while (running) {
        puts("\n1. Compress log");
        puts("2. Decompress log");
        puts("3. Verify restored log");
        puts("4. Exit");
        if (!read_line("Select option: ", choice, sizeof choice))
            break;

        if (strlen(choice) != 1) {
            puts("Invalid option. Choose 1-4.");
            continue;
        }
        switch (choice[0]) {
        case '1': compress_log(); break;
        case '2': decompress_log(); break;
        case '3': verify_restored(); break;
        case '4': running = 0; break;
        default:  puts("Invalid option. Choose 1-4.");
        }
    }

    puts("Goodbye.");
    return 0;
}
