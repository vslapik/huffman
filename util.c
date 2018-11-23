#include <ctype.h>
#include <inttypes.h>
#include "util.h"
#include "huffman.h"

char *hcode2str(const hcode_t *code)
{
    char *s = umalloc(code->len + 1);
    for (size_t i = 0; i < code->len; i++)
    {
       s[i] = (bool)(code->code & (1LLU << i)) + '0';
    }
    s[code->len] = 0;

    return s;
}

hcode_t str2hcode(const char *str, size_t str_len)
{
    hcode_t h = {.code = 0, .len = str_len};
    for (size_t i = 0; i < str_len; i++)
    {
        if (str[i] - '0')
        {
            h.code |= 1 << i;
        }
    }

    return h;
}

char *escape_symbol(int in)
{
    char *out;
    if (in > 255)
    {
        out = ustring_fmt("\\x{%04x}", in);
    }
    else
    {
        if (isprint(in) && (in != ' ') && (in != '\\'))
        {
            out = ustring_fmt("%c", in);
        }
        else
        {
            out = ustring_fmt("\\x%02x", (unsigned char)in);
        }
    }

    return out;
}

void dump_table(const htable_t *table, const hstat_t *stat)
{
    size_t sum = 0;
    size_t count = 0;
    size_t min_code_len = SIZE_MAX;
    size_t max_code_len = 0;

    fprintf(stdout, "Character    Frequency    Length    Huffman Code\n");
    for (int i = 0; i < HCODES_TABLE_SIZE; i++)
    {
        if (stat->frequencies[i])
        {
            size_t len = table->hcodes[i].len;
            char *ec = escape_symbol(i);
            char *hcode = hcode2str(&table->hcodes[i]);
            if (len > max_code_len)
            {
                max_code_len = len;
            }
            if (len < min_code_len)
            {
                min_code_len = len;
            }
            sum += len * stat->frequencies[i];
            count += stat->frequencies[i];
            printf("%7s%14"PRIu32"%10"PRIu32"      %s\n",
                ec,
                stat->frequencies[i],
                table->hcodes[i].len,
                hcode
            );
            ufree(ec);
            ufree(hcode);
        }
    }
    printf("min/max/mean code len: %zu, %zu, %f\n",
           min_code_len, max_code_len, (double)sum/count);
}

typedef struct {
    FILE *f;
    int page;
} node_data;

static void dump_node(const hnode_t *node, void *cb_data, char *path, size_t path_len)
{
    const char *color = NULL;

    UASSERT(cb_data);
    node_data *nd = cb_data;
    UASSERT(nd->f);

    if (!node->left && !node->right)
        color = ", fillcolor=yellow";
    else
        color = ", fillcolor=gray";

    if (node->highlight)
        color = ", fillcolor=red";

    fprintf(nd->f, "    \"%s\" [style=filled%s,label=\"%s\\n%zu\"];\n",
            node->code_as_str, color, node->code_as_str, node->frequency);
    if (node->left)
    {
        fprintf(nd->f, "    \"%s\" -> \"%s\" [label=0];\n",
                node->code_as_str, node->left->code_as_str);
    }
    if (node->right)
    {
        fprintf(nd->f, "    \"%s\" -> \"%s\" [label=1];\n",
                node->code_as_str, node->right->code_as_str);
    }
}

void generate_graph(const ugeneric_t *nodes, size_t count, size_t page)
{
    // Assumed that there are count nodes in array pointed by *node
    UASSERT(nodes);
    UASSERT(count);

    char tmp[sizeof("treeXXX.dot")];

    if (page != -1)
    {
        snprintf(tmp, sizeof(tmp), "tree%03zu.dot", page);
    }

    node_data nd;
    nd.page = page;
    nd.f = fopen(tmp, "w+");
    fprintf(nd.f, "digraph %zu {\n", page);

    for (size_t i = 0; i < count; i++)
    {
        traverse_htree(G_AS_PTR(nodes[i]), dump_node, &nd, NULL, 0, SIZE_MAX);
    }
    fputs("}\n", nd.f);
    fclose(nd.f);
}
