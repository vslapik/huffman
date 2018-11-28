#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include "huffman.h"
#include "util.h"

static hdecode_lut_t *build_lookup_table(const hnode_t *root, const hcfg_t *cfg)
{
    const hnode_t *node;
    hdecode_lut_item_t *li = NULL;
    size_t nrecords = 1 << cfg->cache_nbits;
    size_t lut_size = nrecords * sizeof(hdecode_lut_item_t);

    hdecode_lut_t *lut = umalloc(sizeof(hdecode_lut_t));

    lut->items = umalloc(lut_size);
    lut->nbits = cfg->cache_nbits;

    if (cfg->verbose)
    {
        printf("Building lookup table (%zu bytes) ... ", lut_size);
    }

    for (size_t i = 0; i < nrecords; i++)
    {
        node = root;
        li = &lut->items[i];
        li->node = NULL;
        li->decoded_data = uzalloc(cfg->cache_nbits + 1);
        li->decoded_data_size = 0;
        li->decoded_bits = 0;
        for (size_t j = 0; j < cfg->cache_nbits; j++)
        {
            node = ((1 << j) & i) ? node->right : node->left;
            if (node->is_leaf)
            {
                li->decoded_data[li->decoded_data_size] = node->code;
                li->decoded_data_size += 1;
                li->decoded_bits = j + 1;
                node = root;
            }
        }
        li->node = node;
    }

    if (cfg->verbose)
    {
        printf("Done.\n");
    }

    return lut;
}

static void destroy_lookup_table(hdecode_lut_t *lut)
{
    if (lut)
    {
        size_t lut_records = 1 << lut->nbits;
        for (size_t i = 0; i < lut_records; i++)
        {
            ufree(lut->items[i].decoded_data);
        }
        ufree(lut->items);
        ufree(lut);
    }
}

hstat_t *build_stat(ufile_reader_t *fr, const hcfg_t *cfg)
{
    UASSERT_INPUT(fr);
    UASSERT_INPUT(cfg);

    umemchunk_t m;
    size_t i = 0;
    size_t t = 0;
    size_t file_size = 0;

    hstat_t *stat = uzalloc(sizeof(*stat));
    if (cfg->verbose)
    {
        file_size = G_AS_SIZE(ufile_reader_get_file_size(fr));
        t = (file_size / cfg->block_size) / 58;
        printf("Building stat: ");
    }

    while (ufile_reader_has_next(fr))
    {
        m = G_AS_MEMCHUNK(ufile_reader_read(fr, cfg->block_size, NULL));
        for (size_t j = 0; j < m.size; j++)
        {
            // TODO: check for uint32_t overflow
            stat->frequencies[((uint8_t *)m.data)[j]]++;
        }
        if (cfg->verbose && i++ > t)
        {
            i = 0;
            printf(".");
            fflush(stdout);
        }
    }
    if (cfg->verbose)
    {
        puts(" Done.");
    }

    return stat;
}

static umemchunk_t encode_block(umemchunk_t input, umemchunk_t *buffer,
                                const hcfg_t *cfg, const htable_t *htable)
{
    int bits_len = 0;
    uint64_t bits = 0; // max code length assumed to be 64
    hcode_t hcode;

    uint8_t *in = input.data;
    uint8_t *buf = buffer->data;
    size_t output_size = 0;

    while (input.size--)
    {
        hcode = htable->hcodes[*in++];
        bits |= hcode.code << bits_len;
        bits_len += hcode.len;

        while (bits_len > 8)
        {
            // One byte for writing a new byte withing this cycle, one more
            // byte for writing the leftover (if any) outside of the cycle
            // and 4 bytes is guard space in case decoder uses lookup-table
            // as its code can touch bytes above the buffer boundary.
            if (output_size + 1 + 1 + 4 > buffer->size)
            {
                size_t new_size = MAX(2 * buffer->size, cfg->block_size);
                buffer->data = urealloc(buffer->data, new_size);
                buf = buffer->data + output_size;
                buffer->size = new_size;
            }
            *buf++ = (uint8_t)bits;
            bits >>= 8;
            bits_len -= 8;
            output_size++;
        }
    }

    // Write leftover, space for this one byte is always assured by
    // allocation above.
    *buf++ = (uint8_t)bits;
    output_size++;

    // Write guard bytes.
    *buf++ = 0;
    *buf++ = 0;
    *buf++ = 0;
    *buf = 0;
    output_size += 4;

    umemchunk_t output = {
        .data = buffer->data,
        .size = output_size,
    };

    return output;
}

uvector_t *encode(ufile_reader_t *fr, ufile_writer_t *fw,
                  const htable_t *htable, const hcfg_t *cfg)
{
    umemchunk_t input, buffer, output;
    uvector_t *blocks = uvector_create();
    uvector_set_void_destroyer(blocks, free);
    block_descriptor_t *bds;
    size_t i = 0;
    size_t t = 0;
    size_t file_size = 0;

    if (cfg->verbose)
    {
        file_size = G_AS_SIZE(ufile_reader_get_file_size(fr));
        t = (file_size / cfg->block_size) / 58;
        printf("Encoding file: ");
    }

    // Encoding buffer allocated and expanded (if needed) by encode_block,
    // passing it by ref.
    buffer.data = 0;
    buffer.size = 0;

    while (ufile_reader_has_next(fr))
    {
        bds = umalloc(sizeof(*bds));
        bds->original_offset = G_AS_SIZE(ufile_reader_get_position(fr));
        input = G_AS_MEMCHUNK(ufile_reader_read(fr, cfg->block_size, NULL));
        output = encode_block(input, &buffer, cfg, htable);
        ufile_writer_write(fw, output);
        bds->original_size = input.size;
        bds->compressed_size = output.size;
        uvector_append(blocks, G_PTR(bds));
        if (cfg->verbose && i++ > t)
        {
            i = 0;
            printf(".");
            fflush(stdout);
        }
    }
    if (cfg->verbose)
    {
        puts(" Done.");
    }

    ufree(buffer.data);

    return blocks;
}

static const unsigned char rmasks[] = {
    0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f
};

static uint32_t _get_bits(uint8_t *data, size_t offset, uint8_t nbits)
{
    uint32_t t = 0;
    uint8_t lbits = 0;
    uint8_t rbits = 0;
    uint8_t i = 0;

    data += offset / 8;

    if (offset % 8)
    {
        lbits = 8 - (offset % 8);
        t |= (*data++ >> (8 - lbits));
        i += lbits;
    }
    while ((nbits - i) >= 8)
    {
        t |= (*data++ << i);
        i += 8;
    }
    rbits = nbits - i;
    if (rbits)
    {
        t |= ((rmasks[rbits] & *data) << i);
    }

    UASSERT(t < (1LL << nbits));

    return t;
}

static umemchunk_t _decode_block(umemchunk_t input, umemchunk_t buffer,
                                size_t original_size, const hcfg_t *cfg,
                                const hnode_t *root, const hdecode_lut_t *lut)
{
    hdecode_lut_item_t *li = NULL;
    uint8_t *in = input.data;
    uint8_t *out = buffer.data;
    size_t bit_offset = 0;
    size_t output_size = 0;
    uint32_t bits;
    uint8_t decoded_data_size;

    while (output_size < original_size)
    {
        bits = _get_bits(in, bit_offset, lut->nbits);
        li = &lut->items[bits];

        // TODO: implement partially decoded sequences
        UASSERT(li->decoded_data_size);
        decoded_data_size = li->decoded_data_size;
        if ((output_size + decoded_data_size) > original_size)
        {
            // Decoding via lookup table can decode more symbols
            // than was present in encoded stream, we need to grab
            // only those which were present in original file.
            decoded_data_size = original_size - output_size;
        }

        memcpy(out + output_size, li->decoded_data, decoded_data_size);
        output_size += decoded_data_size;
        bit_offset += li->decoded_bits;
        /*
        if (li->decoded_bits < lut->nbits)
        {
            bool next_bit;
            const hnode_t *node = li->node;
            uint8_t byte = *(in + bit_offset / 8);
            while (!node->is_leaf)
            {
                if (8 == (bit_offset % 8))
                {
                    byte = *(in + bit_offset / 8);
                }
                next_bit = (1 << (bit_offset % 8)) & byte;
                node = next_bit ? node->right : node->left;
                bit_offset++;
            }
            *(out + output_size) = node->code;
            output_size++;
        }
        */
    }

    umemchunk_t output = {
        .data = buffer.data,
        .size = output_size,
    };

    return output;
}

static umemchunk_t decode_block(umemchunk_t input, umemchunk_t buffer,
                                size_t original_size, const hcfg_t *cfg,
                                const hnode_t *root, const hdecode_lut_t *lut)
{
    uint8_t bitptr = 8;
    uint8_t byte = 0;
    bool next_bit;
    uint8_t *in = input.data;
    uint8_t *out = buffer.data;
    size_t output_size = 0;

    while (output_size < original_size)
    {
        const hnode_t *node = root;
        while (true)
        {
            if (node->is_leaf)
            {
                break;
            }
            if (8 == bitptr)
            {
                byte = *in++;
                bitptr = 0;
            }
            next_bit = (1 << bitptr++) & byte;
            node = next_bit ? node->right : node->left;
        }
        output_size++;
        *out++ = node->code;
    }

    umemchunk_t output = {
        .data = buffer.data,
        .size = output_size,
    };

    return output;
}

void dump_lookup_table(const hdecode_lut_t *lut)
{
    size_t table_size = 1 << lut->nbits;
    for (size_t i = 0; i < table_size; i++)
    {
        char *s = umalloc(lut->nbits + 1);
        for (size_t j = 0; j < lut->nbits; j++)
        {
           s[lut->nbits - j - 1] = (bool)(i & (1LLU << j)) + '0';
        }
        s[lut->nbits] = 0;
        printf("%s: %s, %u\n", s, lut->items[i].decoded_data, lut->items[i].decoded_data_size);
        ufree(s);
    }
}

void decode(ufile_reader_t *fr, ufile_writer_t *fw, const hnode_t *root,
            const huffman_archive_header_t *hdr, const hcfg_t *cfg)
{
    const block_descriptor_t *bds;
    hdecode_lut_t *lut = NULL;
    umemchunk_t input, buffer, output;

    size_t j = 0;
    size_t t = 0;

    if (cfg->cache_nbits)
    {
        lut = build_lookup_table(root, cfg);
        if (cfg->dump_lookup_table)
        {
            dump_lookup_table(lut);
        }
    }

    if (cfg->verbose)
    {
        t = hdr->blocks_count / 58;
        printf("Decoding file: ");
    }

    buffer.data = NULL;
    buffer.size = 0;
    for (size_t i = 0; i < hdr->blocks_count; i++)
    {
        bds = &hdr->blocks[i];
        input = G_AS_MEMCHUNK(ufile_reader_read(fr, bds->compressed_size, NULL));
        if (bds->original_size > buffer.size)
        {
            // Initial allocation or reallocating if existing buffer size is not enough.
            ufree(buffer.data);
            buffer.data = umalloc(bds->original_size);
            buffer.size = bds->original_size;
        }
        output = (lut ? _decode_block : decode_block)(input, buffer, bds->original_size, cfg, root, lut);

        // TODO: set position for writing a new block from bds->original_offset.
        ufile_writer_write(fw, output);
        if (cfg->verbose && j++ > t)
        {
            j = 0;
            printf(".");
            fflush(stdout);
        }
    }
    if (cfg->verbose)
    {
        puts(" Done.");
    }

    // Free decoding buffer.
    ufree(buffer.data);
    destroy_lookup_table(lut);
}

static int compare_hnodes(const void *hnode1, const void *hnode2)
{
    const hnode_t *n1 = hnode1;
    const hnode_t *n2 = hnode2;

    // Make sure highlighted nodes are at the top of the heap.
    if (n1->highlight)
    {
        return -1;
    }
    else if (n2->highlight)
    {
        return 1;
    }
    else
    {
        return n1->frequency - n2->frequency;
    }
}

hnode_t *build_tree(const hcfg_t *cfg, const hstat_t *stat)
{
    hnode_t *node;
    size_t page = 0;

    size_t j = 0;
    size_t t = 0;

    // Create leaves and put them to the heap.
    uheap_t *h = uheap_create();
    uheap_set_void_comparator(h, compare_hnodes);
    for (size_t i = 0; i < HCODES_TABLE_SIZE; i++)
    {
        if (stat->frequencies[i])
        {
            node = umalloc(sizeof(*node));
            node->left = NULL;
            node->right = NULL;
            node->is_leaf = true;
            node->code = i;
            node->code_as_str = escape_symbol(i);
            node->highlight = false;
            node->frequency = stat->frequencies[i];
            uheap_push(h, G_PTR(node));
        }
    }

    if (cfg->dump_tree)
    {
        generate_graph(uheap_get_cells(h), uheap_get_size(h), page++);
    }
    if (cfg->verbose)
    {
        t = uheap_get_size(h) / 58;
        printf("Building tree: ");
    }

    // Create nodes.
    hnode_t *n1, *n2;
    while (uheap_get_size(h) > 1)
    {
        n1 = G_AS_PTR(uheap_pop(h));
        n2 = G_AS_PTR(uheap_pop(h));

        if (cfg->dump_tree)
        {
            n1->highlight = true;
            n2->highlight = true;
            uheap_push(h, G_PTR(n1));
            uheap_push(h, G_PTR(n2));
            generate_graph(uheap_get_cells(h), uheap_get_size(h), page++);
            n1 = G_AS_PTR(uheap_pop(h));
            n2 = G_AS_PTR(uheap_pop(h));
            n1->highlight = false;
            n2->highlight = false;
        }
        node = umalloc(sizeof(*node));
        node->left = n1;
        node->right = n2;
        node->is_leaf = false;
        node->code = -1;
        node->code_as_str = ustring_fmt("%s%s", n1->code_as_str, n2->code_as_str);
        node->highlight = false;
        node->frequency = n1->frequency + n2->frequency;
        uheap_push(h, G_PTR(node));
        if (cfg->dump_tree)
        {
            node->highlight = true;
            generate_graph(uheap_get_cells(h), uheap_get_size(h), page++);
            node->highlight = false;
        }
        if (cfg->verbose && j++ > t)
        {
            j = 0;
            printf(".");
            fflush(stdout);
        }
    }
    if (cfg->verbose)
    {
        puts(" Done.");
    }

    hnode_t *root = G_AS_PTR(uheap_pop(h));
    UASSERT(uheap_is_empty(h));
    uheap_destroy(h);
    return root;
}

void traverse_htree(const hnode_t *node, traverse_cb cb, void *cb_data,
                    char *path, size_t path_len, size_t max_depth)
{
    UASSERT_INPUT(node);
    UASSERT_INPUT(cb_data);
    UASSERT_INPUT(path);

    if (path_len == max_depth)
    {
        cb(node, cb_data, path, path_len);
        return;
    }

    if (node->left)
    {
        if (path)
        {
            UASSERT(path_len < MAX_HCODE_LENGTH);
            path[path_len] = '0';
            path_len++;
        }
        traverse_htree(node->left, cb, cb_data, path, path_len, max_depth);
        if (path)
        {
            path_len--;
            path[path_len] = 0;
        }
    }

    if (node->right)
    {
        if (path)
        {
            UASSERT(path_len < MAX_HCODE_LENGTH);
            path[path_len] = '1';
            path_len++;
        }
        traverse_htree(node->right, cb, cb_data, path, path_len, max_depth);
        if (path)
        {
           path_len--;
           path[path_len] = 0;
        }
    }

    cb(node, cb_data, path, path_len);
}

static void gather_hcode(const hnode_t *hnode, void *cb_data,
                         char *path, size_t path_len)
{
    UASSERT_INPUT(hnode);
    UASSERT_INPUT(cb_data);
    UASSERT_INPUT(path);

    hcode_t *hcodes = cb_data;
    hcode_t hcode = {0};

    if (hnode->is_leaf)
    {
        for (size_t i = 0; i < path_len; i++)
        {
            if (path[i] - '0')
            {
                hcode.code |= (1LLU << i);
            }
        }
        hcode.len = path_len;
        hcodes[hnode->code] = hcode;
    }
}

htable_t *build_codes(const hnode_t *root, const hcfg_t *cfg)
{
    UASSERT_INPUT(root);
    UASSERT_INPUT(cfg);

    char path[MAX_HCODE_LENGTH + 1] = {0};
    htable_t *table = uzalloc(sizeof(*table));
    traverse_htree(root, gather_hcode, table->hcodes, path, 0, SIZE_MAX);

    return table;
}

static void destroy_hnode(const hnode_t *hnode, void *cb_data,
                          char *path, size_t path_len)
{
    // Cast away const qualifier imposed by callback signature.
    hnode_t *node = (hnode_t*)hnode;
    ufree(node->code_as_str);
    ufree(node);
}

void destroy_tree(hnode_t *root)
{
    UASSERT_INPUT(root);
    traverse_htree(root, destroy_hnode, NULL, NULL, 0, SIZE_MAX);
}
