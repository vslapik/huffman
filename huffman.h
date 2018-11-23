#ifndef __HUFFMAN_H__
#define __HUFFMAN_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <ugeneric.h>

// App config.
typedef struct {
    char *input_file;
    char *output_file;
    size_t block_size;
    uint8_t cache_nbits;
    bool verbose;
    bool dump_tree;
    bool dump_table;
    bool dump_lookup_table;
    bool dry_run;
    bool extract_mode;
    bool dump_blocks_map;
} hcfg_t;

// Huffman tree node.
struct _node {
    unsigned int code;
    char *code_as_str;
    size_t frequency;
    struct _node *left;
    struct _node *right;
    bool highlight;
    bool is_leaf;
};
typedef struct _node hnode_t;

// Huffman code, LSB of code corresponds to root of the htree.
typedef struct {
    uint8_t len;
    uint64_t code;
} hcode_t;

typedef struct {
    uint32_t original_size;
    uint32_t compressed_size;
    uint32_t original_offset;
} block_descriptor_t;

// Code table, index in the table is corresponded symbol code.
#define HCODES_TABLE_SIZE 256

typedef struct {
    uint32_t frequencies[HCODES_TABLE_SIZE];
} hstat_t;

typedef struct {
    hcode_t hcodes[HCODES_TABLE_SIZE];
    double mean_code_len;
} htable_t;

typedef struct {
    hstat_t stat;
    uint32_t blocks_count;
    block_descriptor_t blocks[];
} huffman_archive_header_t;

// Maximum supported len, very pessimistic, much smaller usually.
#define MAX_HCODE_LENGTH 64

hstat_t *build_stat(ufile_reader_t *fr, const hcfg_t *cfg);
uvector_t *encode(ufile_reader_t *fr, ufile_writer_t *fw, const htable_t *htable, const hcfg_t *cfg);
void decode(ufile_reader_t *fr, ufile_writer_t *fw, const hnode_t *root, const huffman_archive_header_t *hdr, const hcfg_t *cfg);

typedef void (*traverse_cb)(const hnode_t *node, void *cb_data, char *path, size_t path_len);
void traverse_htree(const hnode_t *node, traverse_cb cb, void *cb_data, char *path, size_t path_len, size_t max_depth);

hnode_t *build_tree(const hcfg_t *cfg, const hstat_t *stat);
htable_t *build_codes(const hnode_t *root, const hcfg_t *cfg);
void destroy_tree(hnode_t *root);

// Lookup table item.
typedef struct {
    const hnode_t *node;
    uint8_t *decoded_data;
    uint8_t decoded_data_size;
    uint8_t decoded_bits;
} hdecode_lut_item_t;

// Lookup table.
typedef struct {
    hdecode_lut_item_t *items;
    uint8_t nbits;
} hdecode_lut_t;

#endif
