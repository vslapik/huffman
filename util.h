#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "huffman.h"

char *hcode2str(const hcode_t *code);
hcode_t str2hcode(const char *str, size_t str_len);
char *escape_symbol(int in);
char *escape_string(const char *in);
void dump_table(const htable_t *table, const hstat_t *stat);
void generate_graph(const ugeneric_t *nodes, size_t count, size_t page);

#endif
