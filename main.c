#include <ugeneric.h>
#include "huffman.h"
#include "util.h"

const char SIGNATURE[] = "PKHUF";
const char *VER = "Huffman archiver, "__DATE__" "__TIME__ ".";

static void compress(const char *input_file, const char *output_file, const hcfg_t *cfg);
static void extract(const char *input_file, const char *output_file, const hcfg_t *cfg);

static char *serialize_block(const void *block, size_t *output_size)
{
    const block_descriptor_t *bds = block;
    const char *fmt = "{\"original_size\": %zu, \"compressed_size\": %zu, \"original_offset\": %zu}";
    return ustring_fmt_sized(fmt, output_size, bds->original_size, bds->compressed_size, bds->original_offset);
}

static huffman_archive_header_t *allocate_header(size_t input_size, hstat_t *stat, const hcfg_t *cfg)
{
    huffman_archive_header_t *hdr = ucalloc(1, sizeof(*hdr));
    hdr->blocks_count = input_size / cfg->block_size + (bool)(input_size % cfg->block_size);
    memcpy(&hdr->stat, stat, sizeof(*stat));
    return hdr;
}

static void compress(const char *input_file, const char *output_file, const hcfg_t *cfg)
{
    uvector_t *blocks;
    size_t input_size;
    ufile_reader_t *fr;
    ufile_writer_t *fw;

    // Gather statistics.
    fr = G_AS_PTR(ufile_reader_create(input_file, cfg->block_size));
    input_size = G_AS_SIZE(ufile_reader_get_file_size(fr));
    if (input_size == 0)
    {
        fprintf(stderr, "Error: input file is empty.\n");
        exit(EXIT_FAILURE);
    }
    hstat_t *stat = build_stat(fr, cfg);

    // Build Huffman tree.
    hnode_t *root = build_tree(cfg, stat);

    // Build codes.
    htable_t *table = build_codes(root, cfg);

    if (cfg->dump_table)
    {
        dump_table(table, stat);
    }

    // Allocate archive header.
    huffman_archive_header_t *hdr = allocate_header(input_size, stat, cfg);
    size_t full_header_size = sizeof(*hdr) + hdr->blocks_count * sizeof(block_descriptor_t);
    fw = G_AS_PTR(ufile_writer_create(output_file));
    ufile_writer_set_position(fw, full_header_size);

    // Encode.
    ufile_reader_set_position(fr, 0);
    blocks = encode(fr, fw, table, cfg);
    UASSERT(uvector_get_size(blocks) == hdr->blocks_count);

    // Write archive header.
    umemchunk_t m = {.data = hdr, .size = sizeof(*hdr)};
    ufile_writer_set_position(fw, 0);
    ufile_writer_write(fw, m);
    ugeneric_t *a = uvector_get_cells(blocks);
    for (size_t i = 0; i < hdr->blocks_count; i++)
    {
        block_descriptor_t *bds = G_AS_PTR(a[i]);
        m.data = bds;
        m.size = sizeof(*bds);
        ufile_writer_write(fw, m);
    }

    if (cfg->dump_blocks_map)
    {
        uvector_set_void_serializer(blocks, serialize_block);
        uvector_print(blocks);
    }

    uvector_destroy(blocks);

    // Cleanup.
    ufile_reader_destroy(fr);
    ufile_writer_destroy(fw);
    ufree(hdr);
    ufree(stat);
    ufree(table);
    destroy_tree(root);
}

static void extract(const char *input_file, const char *output_file, const hcfg_t *cfg)
{
    umemchunk_t m;
    size_t input_size;

    // Load archive header.
    ufile_reader_t *fr = G_AS_PTR(ufile_reader_create(input_file, cfg->block_size));
    input_size  = G_AS_SIZE(ufile_reader_get_file_size(fr));
    if (input_size == 0)
    {
        fprintf(stderr, "Error: input file is empty.\n");
        exit(EXIT_FAILURE);
    }
    m = G_AS_MEMCHUNK(ufile_reader_read(fr, sizeof(huffman_archive_header_t), NULL));
    huffman_archive_header_t *hdr = m.data;

    size_t full_header_size = sizeof(huffman_archive_header_t) + sizeof(block_descriptor_t) * hdr->blocks_count;
    hdr = umalloc(full_header_size);
    ufile_reader_set_position(fr, 0);
    ufile_reader_read(fr, full_header_size, hdr);

    if (cfg->dump_blocks_map)
    {
        printf("Blocks map: [");
        for (size_t i = 0; i < hdr->blocks_count; i++)
        {
            char *str = serialize_block(&hdr->blocks[i], NULL);
            printf("%s", str);
            ufree(str);
            if (i < hdr->blocks_count - 1)
            {
                printf(", ");
            }
        }
        printf("]\n");
    }

    // Build Huffman tree.
    hnode_t *root = build_tree(cfg, &hdr->stat);

    if (cfg->dump_table)
    {
        // Build codes.
        htable_t *table = build_codes(root, cfg);
        dump_table(table, &hdr->stat);
        ufree(table);
    }

    // Decode.
    ufile_writer_t *fw = G_AS_PTR(ufile_writer_create(output_file));
    decode(fr, fw, root, hdr, cfg);

    // Cleanup.
    ufile_reader_destroy(fr);
    ufile_writer_destroy(fw);
    destroy_tree(root);
    ufree(hdr);
}
void usage(const char *app_name)
{
    fprintf(stderr, "Usage: %s input_file [-c|-x] output_file [OPTION]...\n", app_name);
    puts("  -c                 compress");
    puts("  -x                 extract");
    puts("  -v                 verbose output");
    puts("  --dump-tree        dump huffman tree creation to dot files");
    puts("  --dump-table       dump huffman codes");
    puts("  --dry-run          copy input to output (i/o test)");
    puts("  --block-size SIZE  block size when reading file (compressing only)");
    puts("  --dump-blocks-map  show blocks headers");
   // puts("  --cache-nbits NBITS cache size in bits (decoder only), [8 ... 24] or 0 to disable");
    puts("  -V                 display software version");
    puts("  -h                 print this message");
}

void parse_cli(int argc, char **argv, hcfg_t *cfg)
{
    if (argc < 2)
    {
        goto bad_cli;
    }

    if (strcmp(argv[1], "-V") == 0)
    {
        puts(VER);
        exit(EXIT_SUCCESS);
    }

    if (strcmp(argv[1], "-h") == 0)
    {
        usage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    cfg->input_file = argv[1];

    int idx = 2;
    while (idx < argc)
    {
        if (strcmp(argv[idx], "-c") == 0)
        {
            idx++;
            if (idx == argc)
            {
                goto bad_cli;
            }
            cfg->extract_mode = false;
            cfg->output_file = argv[idx];

        }
        else if (strcmp(argv[idx], "-x") == 0)
        {
            idx++;
            if (idx == argc)
            {
                goto bad_cli;
            }
            cfg->extract_mode = true;
            cfg->output_file = argv[idx];

        }
        else if (strcmp(argv[idx], "--dry-run") == 0)
        {
            cfg->dry_run = true;
        }
        else if (strcmp(argv[idx], "-v") == 0)
        {
            cfg->verbose = true;
        }
        else if (strcmp(argv[idx], "--dump-tree") == 0)
        {
            cfg->dump_tree = true;
        }
        else if (strcmp(argv[idx], "--dump-table") == 0)
        {
            cfg->dump_table = true;
        }
        else if (strcmp(argv[idx], "--dump-blocks-map") == 0)
        {
            cfg->dump_blocks_map = true;
        }
        else if (strcmp(argv[idx], "--dump-lookup-table") == 0)
        {
            cfg->dump_lookup_table = true;
        }
        else if (strcmp(argv[idx], "--block-size") == 0)
        {
            idx++;
            if (idx == argc)
            {
                goto bad_cli;
            }
            cfg->block_size = atoi(argv[idx]); // TODO: atoi
        }
        else if (strcmp(argv[idx], "--cache-nbits") == 0)
        {
            idx++;
            if (idx == argc)
            {
                goto bad_cli;
            }
            cfg->cache_nbits = atoi(argv[idx]); // TODO: atoi
            if (cfg->cache_nbits < 8 || cfg->cache_nbits > 24)
            {
                cfg->cache_nbits = 0;
                fprintf(stderr, "Error: invalid value %u for --cache-nbits, should be in [8, 24] range, cache is disabled.\n",
                                 cfg->cache_nbits);
            }
        }
        else
        {
            goto bad_cli;
        }
        idx++;
    }

    if (!cfg->input_file || !cfg->output_file)
    {
        goto bad_cli;
    }

    return;

bad_cli:
    usage(argv[0]);
    exit(EXIT_FAILURE);
}

ugeneric_t io_error_handler(ugeneric_t io_error, void *ctx)
{
    ugeneric_error_print(io_error);
    ugeneric_error_destroy(io_error);
    exit(UGENERIC_EXIT_IO);
}

int main(int argc, char **argv)
{
    // Init to default values.
    hcfg_t cfg = {
        .block_size = 131072,
        .verbose = false,
        .dump_tree = false,
        .dump_table = false,
        .dump_lookup_table = false,
        .dry_run = false,
        .input_file = NULL,
        .output_file = NULL,
        .extract_mode = false,
        .dump_blocks_map = false,
        .cache_nbits = 0,
    //    .cache_nbits = 11,
    };

    parse_cli(argc, argv, &cfg);

    libugeneric_set_file_error_handler(io_error_handler, NULL);

    if (strcmp(cfg.input_file, cfg.output_file) == 0)
    {
        fprintf(stderr, "Error: reading and writing to the same file.\n");
        return EXIT_FAILURE;
    }

    if (cfg.dry_run)
    {
        ugeneric_t g;
        umemchunk_t buffer;
        size_t file_size = 0;
        size_t i = 0;
        size_t t = 0;

        ufile_reader_t *fr = G_AS_PTR(ufile_reader_create(cfg.input_file, cfg.block_size));
        ufile_writer_t *fw = G_AS_PTR(ufile_writer_create(cfg.output_file));

        // Try to follow the exact I/O pattern as in real encoding/decoding, i.e. read by
        // big chunks (cfg.block_size) than process them byte by byte and store with big
        // chunks.
        buffer.data = umalloc(cfg.block_size);
        buffer.size = cfg.block_size;

        if (cfg.verbose)
        {
            printf("Dry run mode: copying input file to the ouput file.\n");
            file_size = G_AS_SIZE(ufile_reader_get_file_size(fr));
            t = (file_size / cfg.block_size) / 58;
            printf("Copying file: ");
        }
        while (ufile_reader_has_next(fr))
        {
            g = ufile_reader_read(fr, cfg.block_size, NULL);
            buffer.size = G_AS_MEMCHUNK_SIZE(g);
            for (size_t i = 0; i < buffer.size; i++)
            {
                ((uint8_t *)buffer.data)[i]  = ((uint8_t *)G_AS_MEMCHUNK_DATA(g))[i];
            }
            ufile_writer_write(fw, buffer);

            if (cfg.verbose && i++ > t)
            {
                i = 0;
                printf(".");
                fflush(stdout);
            }
        }
        if (cfg.verbose)
        {
            puts(" Done.");
        }
        ufree(buffer.data);
        ufile_reader_destroy(fr);
        ufile_writer_destroy(fw);
        return EXIT_SUCCESS;
    }

    if (cfg.extract_mode)
    {
        if (cfg.verbose)
        {
            printf("Extracting %s to %s.\n", cfg.input_file, cfg.output_file);
        }
        extract(cfg.input_file, cfg.output_file, &cfg);
    }
    else
    {
        if (cfg.verbose)
        {
            printf("Compressing %s to %s.\n", cfg.input_file, cfg.output_file);
        }
        compress(cfg.input_file, cfg.output_file, &cfg);
    }

    return EXIT_SUCCESS;
}
