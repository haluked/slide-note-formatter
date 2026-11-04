#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include "pdf_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <zlib.h>

#define A4_WIDTH  595.28
#define A4_HEIGHT 841.89

/* -------------------------------------------------------------
 * Dynamic Byte Buffer
 * ------------------------------------------------------------- */
typedef struct {
    unsigned char *data;
    size_t size;
    size_t capacity;
} ByteBuffer;

static void bb_init(ByteBuffer *bb) {
    bb->data = NULL;
    bb->size = 0;
    bb->capacity = 0;
}

static void bb_free(ByteBuffer *bb) {
    if (bb->data) {
        free(bb->data);
        bb->data = NULL;
    }
    bb->size = 0;
    bb->capacity = 0;
}

static int bb_ensure(ByteBuffer *bb, size_t extra) {
    if (bb->size + extra > bb->capacity) {
        size_t new_cap = bb->capacity ? bb->capacity * 2 : 1024;
        while (new_cap < bb->size + extra) new_cap *= 2;
        unsigned char *p = (unsigned char *)realloc(bb->data, new_cap);
        if (!p) return 0;
        bb->data = p;
        bb->capacity = new_cap;
    }
    return 1;
}

static int bb_append(ByteBuffer *bb, const void *data, size_t len) {
    if (!len) return 1;
    if (!bb_ensure(bb, len)) return 0;
    memcpy(bb->data + bb->size, data, len);
    bb->size += len;
    return 1;
}

static int bb_append_str(ByteBuffer *bb, const char *str) {
    return bb_append(bb, str, strlen(str));
}

static int bb_printf(ByteBuffer *bb, const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n < 0) return 0;
    if ((size_t)n < sizeof(buf)) {
        return bb_append(bb, buf, n);
    }
    char *big = (char *)malloc(n + 1);
    if (!big) return 0;
    va_start(args, fmt);
    vsnprintf(big, n + 1, fmt, args);
    va_end(args);
    int ok = bb_append(bb, big, n);
    free(big);
    return ok;
}

/* -------------------------------------------------------------
 * PDF Object Model
 * ------------------------------------------------------------- */
typedef enum {
    PDF_TYPE_NULL = 0,
    PDF_TYPE_BOOL,
    PDF_TYPE_INT,
    PDF_TYPE_REAL,
    PDF_TYPE_STRING,
    PDF_TYPE_HEX_STRING,
    PDF_TYPE_NAME,
    PDF_TYPE_ARRAY,
    PDF_TYPE_DICT,
    PDF_TYPE_REF
} PDFType;

struct PDFObj;

typedef struct {
    char *key;
    struct PDFObj *val;
} PDFDictEntry;

typedef struct PDFObj {
    PDFType type;
    union {
        int b_val;
        long long i_val;
        double r_val;
        struct {
            char *buf;
            size_t len;
        } str;
        char *name;
        struct {
            struct PDFObj **items;
            size_t count;
            size_t capacity;
        } arr;
        struct {
            PDFDictEntry *entries;
            size_t count;
            size_t capacity;
        } dict;
        struct {
            int num;
            int gen;
        } ref;
    } u;
} PDFObj;

static void pdf_obj_free(PDFObj *obj);

static PDFObj *pdf_obj_new(PDFType t) {
    PDFObj *o = (PDFObj *)calloc(1, sizeof(PDFObj));
    if (o) o->type = t;
    return o;
}

static void pdf_obj_free(PDFObj *obj) {
    if (!obj) return;
    if (obj->type == PDF_TYPE_STRING || obj->type == PDF_TYPE_HEX_STRING) {
        free(obj->u.str.buf);
    } else if (obj->type == PDF_TYPE_NAME) {
        free(obj->u.name);
    } else if (obj->type == PDF_TYPE_ARRAY) {
        for (size_t i = 0; i < obj->u.arr.count; i++) {
            pdf_obj_free(obj->u.arr.items[i]);
        }
        free(obj->u.arr.items);
    } else if (obj->type == PDF_TYPE_DICT) {
        for (size_t i = 0; i < obj->u.dict.count; i++) {
            free(obj->u.dict.entries[i].key);
            pdf_obj_free(obj->u.dict.entries[i].val);
        }
        free(obj->u.dict.entries);
    }
    free(obj);
}

static int pdf_dict_add(PDFObj *dict, const char *key, PDFObj *val) {
    if (!dict || dict->type != PDF_TYPE_DICT || !key || !val) return 0;
    if (dict->u.dict.count >= dict->u.dict.capacity) {
        size_t ncap = dict->u.dict.capacity ? dict->u.dict.capacity * 2 : 8;
        PDFDictEntry *p = (PDFDictEntry *)realloc(dict->u.dict.entries, ncap * sizeof(PDFDictEntry));
        if (!p) return 0;
        dict->u.dict.entries = p;
        dict->u.dict.capacity = ncap;
    }
    dict->u.dict.entries[dict->u.dict.count].key = _strdup(key);
    dict->u.dict.entries[dict->u.dict.count].val = val;
    dict->u.dict.count++;
    return 1;
}

static PDFObj *pdf_dict_get(const PDFObj *dict, const char *key) {
    if (!dict || dict->type != PDF_TYPE_DICT || !key) return NULL;
    for (size_t i = 0; i < dict->u.dict.count; i++) {
        if (strcmp(dict->u.dict.entries[i].key, key) == 0) {
            return dict->u.dict.entries[i].val;
        }
    }
    return NULL;
}

static int pdf_array_add(PDFObj *arr, PDFObj *val) {
    if (!arr || arr->type != PDF_TYPE_ARRAY || !val) return 0;
    if (arr->u.arr.count >= arr->u.arr.capacity) {
        size_t ncap = arr->u.arr.capacity ? arr->u.arr.capacity * 2 : 4;
        PDFObj **p = (PDFObj **)realloc(arr->u.arr.items, ncap * sizeof(PDFObj *));
        if (!p) return 0;
        arr->u.arr.items = p;
        arr->u.arr.capacity = ncap;
    }
    arr->u.arr.items[arr->u.arr.count++] = val;
    return 1;
}

/* -------------------------------------------------------------
 * PDF Parser / Lexer
 * ------------------------------------------------------------- */
typedef struct {
    const unsigned char *data;
    size_t size;
    size_t pos;
} PDFParser;

static void parser_skip_whitespace(PDFParser *p) {
    while (p->pos < p->size) {
        unsigned char c = p->data[p->pos];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0' || c == '\f') {
            p->pos++;
        } else if (c == '%') {
            p->pos++;
            while (p->pos < p->size && p->data[p->pos] != '\r' && p->data[p->pos] != '\n') {
                p->pos++;
            }
        } else {
            break;
        }
    }
}

static int parser_next_token(PDFParser *p, char *token, size_t max_len) {
    parser_skip_whitespace(p);
    if (p->pos >= p->size) return 0;

    size_t idx = 0;
    unsigned char c = p->data[p->pos];

    if (c == '<') {
        token[idx++] = c;
        p->pos++;
        if (p->pos < p->size && p->data[p->pos] == '<') {
            token[idx++] = '<';
            p->pos++;
        }
    } else if (c == '>') {
        token[idx++] = c;
        p->pos++;
        if (p->pos < p->size && p->data[p->pos] == '>') {
            token[idx++] = '>';
            p->pos++;
        }
    } else if (c == '[' || c == ']' || c == '(' || c == ')') {
        token[idx++] = c;
        p->pos++;
    } else if (c == '/') {
        token[idx++] = c;
        p->pos++;
        while (p->pos < p->size && idx + 1 < max_len) {
            unsigned char ch = p->data[p->pos];
            if (ch <= 32 || ch == '%' || ch == '/' || ch == '<' || ch == '>' || ch == '[' || ch == ']' || ch == '(' || ch == ')') {
                break;
            }
            token[idx++] = ch;
            p->pos++;
        }
    } else {
        while (p->pos < p->size && idx + 1 < max_len) {
            unsigned char ch = p->data[p->pos];
            if (ch <= 32 || ch == '%' || ch == '/' || ch == '<' || ch == '>' || ch == '[' || ch == ']' || ch == '(' || ch == ')') {
                break;
            }
            token[idx++] = ch;
            p->pos++;
        }
    }
    token[idx] = '\0';
    return 1;
}

static PDFObj *parser_parse_obj(PDFParser *p);

static PDFObj *parser_parse_string(PDFParser *p) {
    p->pos++; /* skip '(' */
    ByteBuffer bb;
    bb_init(&bb);
    int depth = 1;

    while (p->pos < p->size) {
        unsigned char c = p->data[p->pos++];
        if (c == '\\') {
            if (p->pos < p->size) {
                unsigned char ec = p->data[p->pos++];
                if (ec == 'n') bb_append(&bb, "\n", 1);
                else if (ec == 'r') bb_append(&bb, "\r", 1);
                else if (ec == 't') bb_append(&bb, "\t", 1);
                else if (ec == 'b') bb_append(&bb, "\b", 1);
                else if (ec == 'f') bb_append(&bb, "\f", 1);
                else if (ec == '(' || ec == ')' || ec == '\\') bb_append(&bb, &ec, 1);
                else if (ec >= '0' && ec <= '7') {
                    int oct = ec - '0';
                    if (p->pos < p->size && p->data[p->pos] >= '0' && p->data[p->pos] <= '7') {
                        oct = oct * 8 + (p->data[p->pos++] - '0');
                        if (p->pos < p->size && p->data[p->pos] >= '0' && p->data[p->pos] <= '7') {
                            oct = oct * 8 + (p->data[p->pos++] - '0');
                        }
                    }
                    unsigned char b = (unsigned char)oct;
                    bb_append(&bb, &b, 1);
                } else {
                    bb_append(&bb, &ec, 1);
                }
            }
        } else if (c == '(') {
            depth++;
            bb_append(&bb, &c, 1);
        } else if (c == ')') {
            depth--;
            if (depth == 0) break;
            bb_append(&bb, &c, 1);
        } else {
            bb_append(&bb, &c, 1);
        }
    }
    PDFObj *res = pdf_obj_new(PDF_TYPE_STRING);
    res->u.str.buf = (char *)bb.data;
    res->u.str.len = bb.size;
    return res;
}

static PDFObj *parser_parse_hex_string(PDFParser *p) {
    p->pos++; /* skip '<' */
    ByteBuffer bb;
    bb_init(&bb);
    int first = -1;

    while (p->pos < p->size) {
        unsigned char c = p->data[p->pos++];
        if (c == '>') break;
        int val = -1;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        if (val >= 0) {
            if (first < 0) {
                first = val;
            } else {
                unsigned char b = (unsigned char)((first << 4) | val);
                bb_append(&bb, &b, 1);
                first = -1;
            }
        }
    }
    if (first >= 0) {
        unsigned char b = (unsigned char)(first << 4);
        bb_append(&bb, &b, 1);
    }
    PDFObj *res = pdf_obj_new(PDF_TYPE_HEX_STRING);
    res->u.str.buf = (char *)bb.data;
    res->u.str.len = bb.size;
    return res;
}

static PDFObj *parser_parse_dict(PDFParser *p) {
    p->pos += 2; /* skip '<<' */
    PDFObj *dict = pdf_obj_new(PDF_TYPE_DICT);

    while (p->pos < p->size) {
        parser_skip_whitespace(p);
        if (p->pos + 1 < p->size && p->data[p->pos] == '>' && p->data[p->pos + 1] == '>') {
            p->pos += 2;
            break;
        }
        char token[256];
        if (!parser_next_token(p, token, sizeof(token))) break;
        if (token[0] == '>' && token[1] == '>') break;
        if (token[0] != '/') continue;

        PDFObj *val = parser_parse_obj(p);
        if (val) {
            pdf_dict_add(dict, token + 1, val);
        }
    }
    return dict;
}

static PDFObj *parser_parse_array(PDFParser *p) {
    p->pos++; /* skip '[' */
    PDFObj *arr = pdf_obj_new(PDF_TYPE_ARRAY);

    while (p->pos < p->size) {
        parser_skip_whitespace(p);
        if (p->pos < p->size && p->data[p->pos] == ']') {
            p->pos++;
            break;
        }
        PDFObj *item = parser_parse_obj(p);
        if (item) {
            pdf_array_add(arr, item);
        } else {
            break;
        }
    }
    return arr;
}

static PDFObj *parser_parse_obj(PDFParser *p) {
    parser_skip_whitespace(p);
    if (p->pos >= p->size) return NULL;

    int c = p->data[p->pos];
    if (c == '(') return parser_parse_string(p);
    if (c == '[') return parser_parse_array(p);
    if (c == '<') {
        if (p->pos + 1 < p->size && p->data[p->pos + 1] == '<') {
            return parser_parse_dict(p);
        } else {
            return parser_parse_hex_string(p);
        }
    }

    size_t save_pos = p->pos;
    char token[256];
    if (!parser_next_token(p, token, sizeof(token))) return NULL;

    if (strcmp(token, "true") == 0) {
        PDFObj *o = pdf_obj_new(PDF_TYPE_BOOL);
        o->u.b_val = 1;
        return o;
    }
    if (strcmp(token, "false") == 0) {
        PDFObj *o = pdf_obj_new(PDF_TYPE_BOOL);
        o->u.b_val = 0;
        return o;
    }
    if (strcmp(token, "null") == 0) {
        return pdf_obj_new(PDF_TYPE_NULL);
    }
    if (token[0] == '/') {
        PDFObj *o = pdf_obj_new(PDF_TYPE_NAME);
        o->u.name = _strdup(token + 1);
        return o;
    }

    char *endptr = NULL;
    long long num = strtoll(token, &endptr, 10);
    if (endptr != token && *endptr == '\0') {
        size_t pos2 = p->pos;
        char tok2[64], tok3[64];
        if (parser_next_token(p, tok2, sizeof(tok2))) {
            char *end2 = NULL;
            long long gen = strtoll(tok2, &end2, 10);
            if (end2 != tok2 && *end2 == '\0') {
                if (parser_next_token(p, tok3, sizeof(tok3)) && strcmp(tok3, "R") == 0) {
                    PDFObj *ref = pdf_obj_new(PDF_TYPE_REF);
                    ref->u.ref.num = (int)num;
                    ref->u.ref.gen = (int)gen;
                    return ref;
                }
            }
        }
        p->pos = pos2;
        PDFObj *o = pdf_obj_new(PDF_TYPE_INT);
        o->u.i_val = num;
        return o;
    }

    double r = strtod(token, &endptr);
    if (endptr != token && *endptr == '\0') {
        PDFObj *o = pdf_obj_new(PDF_TYPE_REAL);
        o->u.r_val = r;
        return o;
    }

    p->pos = save_pos;
    return NULL;
}

/* -------------------------------------------------------------
 * PDF Cross-Reference & Object Table
 * ------------------------------------------------------------- */
typedef struct {
    int type;            /* 0: free, 1: offset, 2: in ObjStm */
    long long offset;    /* file offset if 1, or obj_stm obj_num if 2 */
    int gen_or_idx;      /* generation number if 1, or index in obj_stm if 2 */
    size_t length;       /* length of object in file if known */
} XRefEntry;

typedef struct {
    const unsigned char *data;
    size_t size;

    XRefEntry *entries;
    size_t count;
    size_t capacity;

    PDFObj *trailer;
} PDFDoc;

static void doc_init(PDFDoc *doc, const unsigned char *data, size_t size) {
    doc->data = data;
    doc->size = size;
    doc->entries = NULL;
    doc->count = 0;
    doc->capacity = 0;
    doc->trailer = NULL;
}

static void doc_free(PDFDoc *doc) {
    if (doc->entries) free(doc->entries);
    if (doc->trailer) pdf_obj_free(doc->trailer);
    doc->entries = NULL;
    doc->count = 0;
    doc->trailer = NULL;
}

static int doc_ensure_entries(PDFDoc *doc, size_t max_obj) {
    if (max_obj >= doc->capacity) {
        size_t ncap = doc->capacity ? doc->capacity * 2 : 1024;
        while (ncap <= max_obj) ncap *= 2;
        XRefEntry *p = (XRefEntry *)realloc(doc->entries, ncap * sizeof(XRefEntry));
        if (!p) return 0;
        memset(p + doc->capacity, 0, (ncap - doc->capacity) * sizeof(XRefEntry));
        doc->entries = p;
        doc->capacity = ncap;
    }
    if (max_obj >= doc->count) {
        doc->count = max_obj + 1;
    }
    return 1;
}

static int doc_set_entry(PDFDoc *doc, int obj_num, int type, long long off, int gen_or_idx) {
    if (obj_num < 0 || obj_num > 2000000) return 0;
    if (!doc_ensure_entries(doc, obj_num)) return 0;
    if (doc->entries[obj_num].type == 0 || doc->entries[obj_num].type == 1) {
        doc->entries[obj_num].type = type;
        doc->entries[obj_num].offset = off;
        doc->entries[obj_num].gen_or_idx = gen_or_idx;
    }
    return 1;
}

/* Parse XRef stream */
static int doc_parse_xref_stream(PDFDoc *doc, long long offset, long long *prev_offset) {
    PDFParser p = { doc->data, doc->size, (size_t)offset };
    char tok[64];
    if (!parser_next_token(&p, tok, sizeof(tok))) return 0;
    if (!parser_next_token(&p, tok, sizeof(tok))) return 0;
    if (!parser_next_token(&p, tok, sizeof(tok))) return 0;

    PDFObj *dict = parser_parse_obj(&p);
    if (!dict || dict->type != PDF_TYPE_DICT) {
        pdf_obj_free(dict);
        return 0;
    }

    if (!doc->trailer) {
        doc->trailer = dict;
    }

    PDFObj *prev_obj = pdf_dict_get(dict, "Prev");
    if (prev_obj && prev_obj->type == PDF_TYPE_INT && prev_offset) {
        *prev_offset = prev_obj->u.i_val;
    } else if (prev_offset) {
        *prev_offset = 0;
    }

    PDFObj *w_obj = pdf_dict_get(dict, "W");
    if (!w_obj || w_obj->type != PDF_TYPE_ARRAY || w_obj->u.arr.count < 3) {
        if (doc->trailer != dict) pdf_obj_free(dict);
        return 0;
    }
    int w0 = (int)w_obj->u.arr.items[0]->u.i_val;
    int w1 = (int)w_obj->u.arr.items[1]->u.i_val;
    int w2 = (int)w_obj->u.arr.items[2]->u.i_val;
    int entry_size = w0 + w1 + w2;
    if (entry_size <= 0) {
        if (doc->trailer != dict) pdf_obj_free(dict);
        return 0;
    }

    PDFObj *size_obj = pdf_dict_get(dict, "Size");
    int total_size = size_obj ? (int)size_obj->u.i_val : 0;
    PDFObj *index_obj = pdf_dict_get(dict, "Index");

    parser_skip_whitespace(&p);
    if (!parser_next_token(&p, tok, sizeof(tok)) || strcmp(tok, "stream") != 0) {
        if (doc->trailer != dict) pdf_obj_free(dict);
        return 0;
    }
    if (p.pos < p.size && p.data[p.pos] == '\r') p.pos++;
    if (p.pos < p.size && p.data[p.pos] == '\n') p.pos++;
    const unsigned char *stream_start = p.data + p.pos;

    PDFObj *len_obj = pdf_dict_get(dict, "Length");
    size_t stream_len = len_obj ? (size_t)len_obj->u.i_val : 0;
    if (stream_len == 0 || p.pos + stream_len > p.size) {
        const unsigned char *end_str = (const unsigned char *)strstr((const char *)stream_start, "endstream");
        stream_len = end_str ? (end_str - stream_start) : (p.size - p.pos);
    }

    ByteBuffer decomp;
    bb_init(&decomp);
    PDFObj *filter_obj = pdf_dict_get(dict, "Filter");
    int is_flate = filter_obj && filter_obj->type == PDF_TYPE_NAME && strcmp(filter_obj->u.name, "FlateDecode") == 0;

    const unsigned char *xdata = stream_start;
    size_t xdata_len = stream_len;

    if (is_flate) {
        uLongf dest_len = (uLongf)(entry_size * (total_size > 0 ? total_size : 1000) + 1024);
        if (dest_len < 65536) dest_len = 65536;
        bb_ensure(&decomp, dest_len);
        int zres = uncompress(decomp.data, &dest_len, stream_start, (uLong)stream_len);
        if (zres == Z_OK) {
            decomp.size = dest_len;
            xdata = decomp.data;
            xdata_len = dest_len;
        } else {
            dest_len = (uLongf)(stream_len * 20 + 65536);
            bb_ensure(&decomp, dest_len);
            zres = uncompress(decomp.data, &dest_len, stream_start, (uLong)stream_len);
            if (zres == Z_OK) {
                decomp.size = dest_len;
                xdata = decomp.data;
                xdata_len = dest_len;
            }
        }
    }

    int num_sections = 1;
    int def_index[2] = { 0, total_size };
    int *index_arr = def_index;

    if (index_obj && index_obj->type == PDF_TYPE_ARRAY && index_obj->u.arr.count >= 2) {
        num_sections = (int)(index_obj->u.arr.count / 2);
        index_arr = (int *)malloc(num_sections * 2 * sizeof(int));
        for (int i = 0; i < num_sections * 2; i++) {
            index_arr[i] = (int)index_obj->u.arr.items[i]->u.i_val;
        }
    }

    size_t byte_pos = 0;
    for (int sec = 0; sec < num_sections; sec++) {
        int first_obj = index_arr[sec * 2];
        int count = index_arr[sec * 2 + 1];

        for (int i = 0; i < count; i++) {
            if (byte_pos + entry_size > xdata_len) break;
            int type = 1;
            if (w0 > 0) {
                type = 0;
                for (int b = 0; b < w0; b++) type = (type << 8) | xdata[byte_pos++];
            }
            long long field1 = 0;
            for (int b = 0; b < w1; b++) field1 = (field1 << 8) | xdata[byte_pos++];
            int field2 = 0;
            for (int b = 0; b < w2; b++) field2 = (field2 << 8) | xdata[byte_pos++];

            int obj_num = first_obj + i;
            if (type == 1) {
                doc_set_entry(doc, obj_num, 1, field1, field2);
            } else if (type == 2) {
                doc_set_entry(doc, obj_num, 2, field1, field2);
            }
        }
    }

    if (index_arr != def_index) free(index_arr);
    bb_free(&decomp);
    if (doc->trailer != dict) pdf_obj_free(dict);
    return 1;
}

/* Parse classic XRef table */
static int doc_parse_xref_table(PDFDoc *doc, long long offset, long long *prev_offset) {
    PDFParser p = { doc->data, doc->size, (size_t)offset };
    char tok[64];
    if (!parser_next_token(&p, tok, sizeof(tok))) return 0;
    if (strcmp(tok, "xref") != 0) return 0;

    while (p.pos < p.size) {
        parser_skip_whitespace(&p);
        if (!parser_next_token(&p, tok, sizeof(tok))) break;
        if (strcmp(tok, "trailer") == 0) {
            PDFObj *t = parser_parse_obj(&p);
            if (t && t->type == PDF_TYPE_DICT) {
                if (!doc->trailer) doc->trailer = t;
                PDFObj *prv = pdf_dict_get(t, "Prev");
                if (prv && prv->type == PDF_TYPE_INT && prev_offset) {
                    *prev_offset = prv->u.i_val;
                } else if (prev_offset) {
                    *prev_offset = 0;
                }
                if (doc->trailer != t) pdf_obj_free(t);
            }
            break;
        }

        int first_obj = atoi(tok);
        if (!parser_next_token(&p, tok, sizeof(tok))) break;
        int count = atoi(tok);

        for (int i = 0; i < count; i++) {
            parser_skip_whitespace(&p);
            char off_str[32], gen_str[32], type_str[16];
            if (!parser_next_token(&p, off_str, sizeof(off_str))) break;
            if (!parser_next_token(&p, gen_str, sizeof(gen_str))) break;
            if (!parser_next_token(&p, type_str, sizeof(type_str))) break;

            long long off = strtoll(off_str, NULL, 10);
            int gen = atoi(gen_str);
            int obj_num = first_obj + i;
            if (type_str[0] == 'n') {
                doc_set_entry(doc, obj_num, 1, off, gen);
            }
        }
    }
    return 1;
}

static int doc_load_xref(PDFDoc *doc) {
    size_t scan_len = doc->size > 4096 ? 4096 : doc->size;
    const char *tail = (const char *)(doc->data + doc->size - scan_len);
    const char *found = NULL;
    for (size_t i = scan_len - 9; i > 0; i--) {
        if (memcmp(tail + i, "startxref", 9) == 0) {
            found = tail + i;
            break;
        }
    }
    if (!found) return 0;

    PDFParser p = { doc->data, doc->size, (size_t)(found - (const char *)doc->data) + 9 };
    char tok[64];
    if (!parser_next_token(&p, tok, sizeof(tok))) return 0;
    long long xref_offset = strtoll(tok, NULL, 10);

    int loop_guard = 0;
    while (xref_offset > 0 && xref_offset < (long long)doc->size && loop_guard++ < 20) {
        long long prev = 0;
        PDFParser test_p = { doc->data, doc->size, (size_t)xref_offset };
        parser_skip_whitespace(&test_p);
        if (test_p.pos + 4 <= test_p.size && memcmp(test_p.data + test_p.pos, "xref", 4) == 0) {
            if (!doc_parse_xref_table(doc, xref_offset, &prev)) break;
        } else {
            if (!doc_parse_xref_stream(doc, xref_offset, &prev)) break;
        }
        xref_offset = prev;
    }
    return doc->trailer != NULL;
}

/* -------------------------------------------------------------
 * Object Stream & Object Resolution
 * ------------------------------------------------------------- */
static PDFObj *doc_get_object(PDFDoc *doc, int obj_num);

static PDFObj *doc_resolve(PDFDoc *doc, PDFObj *obj) {
    if (!obj) return NULL;
    if (obj->type == PDF_TYPE_REF) {
        return doc_get_object(doc, obj->u.ref.num);
    }
    return obj;
}

static PDFObj *doc_read_from_objstm(PDFDoc *doc, int stm_num, int idx) {
    PDFObj *stm_obj = doc_get_object(doc, stm_num);
    if (!stm_obj || stm_obj->type != PDF_TYPE_DICT) return NULL;

    PDFObj *n_obj = pdf_dict_get(stm_obj, "N");
    PDFObj *first_obj = pdf_dict_get(stm_obj, "First");
    if (!n_obj || !first_obj) return NULL;

    int n_count = (int)n_obj->u.i_val;
    size_t first_off = (size_t)first_obj->u.i_val;

    if (stm_num >= (int)doc->count || doc->entries[stm_num].type != 1) return NULL;
    long long file_off = doc->entries[stm_num].offset;

    PDFParser p = { doc->data, doc->size, (size_t)file_off };
    char tok[64];
    parser_next_token(&p, tok, sizeof(tok));
    parser_next_token(&p, tok, sizeof(tok));
    parser_next_token(&p, tok, sizeof(tok));
    PDFObj *d = parser_parse_obj(&p);
    pdf_obj_free(d);

    parser_skip_whitespace(&p);
    if (!parser_next_token(&p, tok, sizeof(tok)) || strcmp(tok, "stream") != 0) return NULL;
    if (p.pos < p.size && p.data[p.pos] == '\r') p.pos++;
    if (p.pos < p.size && p.data[p.pos] == '\n') p.pos++;

    PDFObj *len_obj = pdf_dict_get(stm_obj, "Length");
    size_t stm_len = len_obj ? (size_t)len_obj->u.i_val : 0;
    if (stm_len == 0 || p.pos + stm_len > p.size) {
        const unsigned char *end_str = (const unsigned char *)strstr((const char *)p.data + p.pos, "endstream");
        stm_len = end_str ? (end_str - (p.data + p.pos)) : (p.size - p.pos);
    }

    ByteBuffer decomp;
    bb_init(&decomp);
    uLongf dest_len = (uLongf)(stm_len * 15 + 65536);
    bb_ensure(&decomp, dest_len);
    int zres = uncompress(decomp.data, &dest_len, p.data + p.pos, (uLong)stm_len);
    if (zres != Z_OK) {
        bb_free(&decomp);
        return NULL;
    }
    decomp.size = dest_len;

    PDFParser stm_p = { decomp.data, decomp.size, 0 };
    size_t target_offset = 0;
    int found = 0;

    for (int i = 0; i < n_count; i++) {
        if (!parser_next_token(&stm_p, tok, sizeof(tok))) break;
        int onum = atoi(tok);
        (void)onum;
        if (!parser_next_token(&stm_p, tok, sizeof(tok))) break;
        size_t off = (size_t)strtoull(tok, NULL, 10);
        if (i == idx) {
            target_offset = first_off + off;
            found = 1;
            break;
        }
    }

    PDFObj *res = NULL;
    if (found && target_offset < decomp.size) {
        stm_p.pos = target_offset;
        res = parser_parse_obj(&stm_p);
    }
    bb_free(&decomp);
    return res;
}

static PDFObj *doc_get_object(PDFDoc *doc, int obj_num) {
    if (obj_num <= 0 || obj_num >= (int)doc->count) return NULL;
    XRefEntry *e = &doc->entries[obj_num];

    if (e->type == 1) {
        PDFParser p = { doc->data, doc->size, (size_t)e->offset };
        char tok[64];
        if (!parser_next_token(&p, tok, sizeof(tok))) return NULL;
        if (!parser_next_token(&p, tok, sizeof(tok))) return NULL;
        if (!parser_next_token(&p, tok, sizeof(tok))) return NULL;
        return parser_parse_obj(&p);
    } else if (e->type == 2) {
        return doc_read_from_objstm(doc, (int)e->offset, e->gen_or_idx);
    }
    return NULL;
}

/* -------------------------------------------------------------
 * Page Tree Extraction
 * ------------------------------------------------------------- */
typedef struct {
    double x0, y0, x1, y1;
    int rotate;
    PDFObj *resources;
    PDFObj *contents;
    int page_obj_num;
} PageInfo;

typedef struct {
    PageInfo *pages;
    size_t count;
    size_t capacity;
} PageList;

static void pagelist_init(PageList *pl) {
    pl->pages = NULL;
    pl->count = 0;
    pl->capacity = 0;
}

static void pagelist_free(PageList *pl) {
    if (pl->pages) free(pl->pages);
    pl->pages = NULL;
    pl->count = 0;
}

static int pagelist_add(PageList *pl, PageInfo info) {
    if (pl->count >= pl->capacity) {
        size_t ncap = pl->capacity ? pl->capacity * 2 : 16;
        PageInfo *p = (PageInfo *)realloc(pl->pages, ncap * sizeof(PageInfo));
        if (!p) return 0;
        pl->pages = p;
        pl->capacity = ncap;
    }
    pl->pages[pl->count++] = info;
    return 1;
}

static void parse_rect(PDFObj *box, double *x0, double *y0, double *x1, double *y1) {
    if (box && box->type == PDF_TYPE_ARRAY && box->u.arr.count >= 4) {
        *x0 = box->u.arr.items[0]->type == PDF_TYPE_INT ? box->u.arr.items[0]->u.i_val : box->u.arr.items[0]->u.r_val;
        *y0 = box->u.arr.items[1]->type == PDF_TYPE_INT ? box->u.arr.items[1]->u.i_val : box->u.arr.items[1]->u.r_val;
        *x1 = box->u.arr.items[2]->type == PDF_TYPE_INT ? box->u.arr.items[2]->u.i_val : box->u.arr.items[2]->u.r_val;
        *y1 = box->u.arr.items[3]->type == PDF_TYPE_INT ? box->u.arr.items[3]->u.i_val : box->u.arr.items[3]->u.r_val;
    }
}

static void collect_pages(PDFDoc *doc, PDFObj *node, double inh_x0, double inh_y0, double inh_x1, double inh_y1, int inh_rot, PDFObj *inh_res, PageList *out_list, int node_num) {
    if (!node) return;
    PDFObj *resolved = doc_resolve(doc, node);
    if (!resolved || resolved->type != PDF_TYPE_DICT) return;

    PDFObj *type_obj = pdf_dict_get(resolved, "Type");
    const char *type_name = (type_obj && type_obj->type == PDF_TYPE_NAME) ? type_obj->u.name : "";

    PDFObj *mbox = pdf_dict_get(resolved, "MediaBox");
    if (!mbox) mbox = pdf_dict_get(resolved, "CropBox");
    double x0 = inh_x0, y0 = inh_y0, x1 = inh_x1, y1 = inh_y1;
    if (mbox) {
        PDFObj *rm = doc_resolve(doc, mbox);
        parse_rect(rm, &x0, &y0, &x1, &y1);
    }

    int rot = inh_rot;
    PDFObj *rot_obj = pdf_dict_get(resolved, "Rotate");
    if (rot_obj) {
        PDFObj *rr = doc_resolve(doc, rot_obj);
        if (rr && rr->type == PDF_TYPE_INT) rot = (int)rr->u.i_val;
    }

    PDFObj *res = inh_res;
    PDFObj *res_obj = pdf_dict_get(resolved, "Resources");
    if (res_obj) {
        res = res_obj;
    }

    if (strcmp(type_name, "Pages") == 0) {
        PDFObj *kids = pdf_dict_get(resolved, "Kids");
        if (kids) {
            PDFObj *rk = doc_resolve(doc, kids);
            if (rk && rk->type == PDF_TYPE_ARRAY) {
                for (size_t i = 0; i < rk->u.arr.count; i++) {
                    int knum = rk->u.arr.items[i]->type == PDF_TYPE_REF ? rk->u.arr.items[i]->u.ref.num : 0;
                    collect_pages(doc, rk->u.arr.items[i], x0, y0, x1, y1, rot, res, out_list, knum);
                }
            }
        }
    } else if (strcmp(type_name, "Page") == 0) {
        PageInfo pi;
        pi.x0 = x0; pi.y0 = y0; pi.x1 = x1; pi.y1 = y1;
        pi.rotate = rot;
        pi.resources = res;
        pi.contents = pdf_dict_get(resolved, "Contents");
        pi.page_obj_num = node_num;
        pagelist_add(out_list, pi);
    }
}

/* -------------------------------------------------------------
 * Extract Content Stream Data
 * ------------------------------------------------------------- */
static int extract_stream_decompressed(PDFDoc *doc, PDFObj *contents_ref, ByteBuffer *out_buf) {
    if (!contents_ref) return 1;
    PDFObj *resolved = doc_resolve(doc, contents_ref);
    if (!resolved) return 1;

    if (resolved->type == PDF_TYPE_ARRAY) {
        for (size_t i = 0; i < resolved->u.arr.count; i++) {
            extract_stream_decompressed(doc, resolved->u.arr.items[i], out_buf);
            bb_append_str(out_buf, "\n");
        }
        return 1;
    }

    if (contents_ref->type != PDF_TYPE_REF) return 1;
    int obj_num = contents_ref->u.ref.num;
    if (obj_num <= 0 || obj_num >= (int)doc->count) return 1;
    if (doc->entries[obj_num].type != 1) return 1;

    long long file_off = doc->entries[obj_num].offset;
    PDFParser p = { doc->data, doc->size, (size_t)file_off };
    char tok[64];
    parser_next_token(&p, tok, sizeof(tok));
    parser_next_token(&p, tok, sizeof(tok));
    parser_next_token(&p, tok, sizeof(tok));
    PDFObj *dict = parser_parse_obj(&p);
    if (!dict || dict->type != PDF_TYPE_DICT) {
        pdf_obj_free(dict);
        return 1;
    }

    parser_skip_whitespace(&p);
    if (!parser_next_token(&p, tok, sizeof(tok)) || strcmp(tok, "stream") != 0) {
        pdf_obj_free(dict);
        return 1;
    }
    if (p.pos < p.size && p.data[p.pos] == '\r') p.pos++;
    if (p.pos < p.size && p.data[p.pos] == '\n') p.pos++;

    PDFObj *len_obj = pdf_dict_get(dict, "Length");
    size_t stream_len = 0;
    if (len_obj) {
        PDFObj *rlen = doc_resolve(doc, len_obj);
        if (rlen && rlen->type == PDF_TYPE_INT) stream_len = (size_t)rlen->u.i_val;
    }
    if (stream_len == 0 || p.pos + stream_len > p.size) {
        const unsigned char *end_str = (const unsigned char *)strstr((const char *)p.data + p.pos, "endstream");
        stream_len = end_str ? (end_str - (p.data + p.pos)) : (p.size - p.pos);
    }

    PDFObj *filt = pdf_dict_get(dict, "Filter");
    int is_flate = 0;
    if (filt) {
        PDFObj *rfilt = doc_resolve(doc, filt);
        if (rfilt && rfilt->type == PDF_TYPE_NAME && strcmp(rfilt->u.name, "FlateDecode") == 0) {
            is_flate = 1;
        }
    }

    if (is_flate) {
        uLongf dest_len = (uLongf)(stream_len * 10 + 65536);
        ByteBuffer tmp;
        bb_init(&tmp);
        bb_ensure(&tmp, dest_len);
        int zres = uncompress(tmp.data, &dest_len, p.data + p.pos, (uLong)stream_len);
        if (zres == Z_OK) {
            tmp.size = dest_len;
            bb_append(out_buf, tmp.data, tmp.size);
        } else {
            bb_append(out_buf, p.data + p.pos, stream_len);
        }
        bb_free(&tmp);
    } else {
        bb_append(out_buf, p.data + p.pos, stream_len);
    }

    pdf_obj_free(dict);
    return 1;
}

static void serialize_obj(ByteBuffer *bb, const PDFObj *obj) {
    if (!obj) return;
    switch (obj->type) {
        case PDF_TYPE_NULL: bb_append_str(bb, "null"); break;
        case PDF_TYPE_BOOL: bb_append_str(bb, obj->u.b_val ? "true" : "false"); break;
        case PDF_TYPE_INT: bb_printf(bb, "%lld", obj->u.i_val); break;
        case PDF_TYPE_REAL: bb_printf(bb, "%.4f", obj->u.r_val); break;
        case PDF_TYPE_NAME: bb_printf(bb, "/%s", obj->u.name); break;
        case PDF_TYPE_STRING: {
            bb_append_str(bb, "(");
            for (size_t i = 0; i < obj->u.str.len; i++) {
                unsigned char c = (unsigned char)obj->u.str.buf[i];
                if (c == '(' || c == ')' || c == '\\') {
                    bb_printf(bb, "\\%c", c);
                } else if (c >= 32 && c <= 126) {
                    bb_append(bb, &c, 1);
                } else {
                    bb_printf(bb, "\\%03o", c);
                }
            }
            bb_append_str(bb, ")");
            break;
        }
        case PDF_TYPE_HEX_STRING: {
            bb_append_str(bb, "<");
            for (size_t i = 0; i < obj->u.str.len; i++) {
                bb_printf(bb, "%02X", (unsigned char)obj->u.str.buf[i]);
            }
            bb_append_str(bb, ">");
            break;
        }
        case PDF_TYPE_ARRAY: {
            bb_append_str(bb, "[");
            for (size_t i = 0; i < obj->u.arr.count; i++) {
                if (i > 0) bb_append_str(bb, " ");
                serialize_obj(bb, obj->u.arr.items[i]);
            }
            bb_append_str(bb, "]");
            break;
        }
        case PDF_TYPE_DICT: {
            bb_append_str(bb, "<<\n");
            for (size_t i = 0; i < obj->u.dict.count; i++) {
                bb_printf(bb, "/%s ", obj->u.dict.entries[i].key);
                serialize_obj(bb, obj->u.dict.entries[i].val);
                bb_append_str(bb, "\n");
            }
            bb_append_str(bb, ">>");
            break;
        }
        case PDF_TYPE_REF: {
            bb_printf(bb, "%d %d R", obj->u.ref.num, obj->u.ref.gen);
            break;
        }
        default: break;
    }
}

/* -------------------------------------------------------------
 * Helvetica Font Metrics & Text Measurement
 * ------------------------------------------------------------- */
static const int helvetica_widths[128] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,
    556, 556, 556, 556, 556, 556, 556, 556, 556, 556,
    278, 278, 584, 584, 584, 556, 1015,
    667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667,
    778, 722, 667, 611, 722, 667, 944, 667, 667, 611,
    278, 278, 278, 469, 556, 333,
    556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556,
    556, 333, 500, 278, 556, 500, 722, 500, 500, 500,
    334, 260, 334, 584, 0
};

static double get_helvetica_text_width(const char *text, double font_size) {
    if (!text) return 0.0;
    double total_units = 0.0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (*p < 128) total_units += helvetica_widths[*p];
        else total_units += 556; /* standard average width for non-ASCII characters */
    }
    return (total_units * font_size) / 1000.0;
}

/* -------------------------------------------------------------
 * Main PDF Processor & Imposer
 * ------------------------------------------------------------- */
int process_pdf_file_w(
    const wchar_t *input_path,
    const wchar_t *output_path,
    const PDFEngineConfig *config,
    PDFProgressCallback progress_cb,
    void *user_data,
    int *out_pages,
    int *out_slides,
    char *error_msg,
    size_t error_msg_size
) {
    if (!input_path || !output_path) {
        if (error_msg) snprintf(error_msg, error_msg_size, "Invalid input or output path.");
        return 1;
    }

    FILE *f_in = _wfopen(input_path, L"rb");
    if (!f_in) {
        if (error_msg) snprintf(error_msg, error_msg_size, "Could not open input PDF file.");
        return 2;
    }

    fseek(f_in, 0, SEEK_END);
    long fsize = ftell(f_in);
    fseek(f_in, 0, SEEK_SET);

    if (fsize < 32) {
        fclose(f_in);
        if (error_msg) snprintf(error_msg, error_msg_size, "Input file too small or invalid PDF.");
        return 3;
    }

    unsigned char *in_data = (unsigned char *)malloc(fsize);
    if (!in_data) {
        fclose(f_in);
        if (error_msg) snprintf(error_msg, error_msg_size, "Out of memory reading input PDF.");
        return 4;
    }

    if (fread(in_data, 1, fsize, f_in) != (size_t)fsize) {
        free(in_data);
        fclose(f_in);
        if (error_msg) snprintf(error_msg, error_msg_size, "Error reading input PDF data.");
        return 5;
    }
    fclose(f_in);

    PDFDoc doc;
    doc_init(&doc, in_data, fsize);
    if (!doc_load_xref(&doc)) {
        doc_free(&doc);
        free(in_data);
        if (error_msg) snprintf(error_msg, error_msg_size, "Failed to parse PDF cross-reference table or catalog.");
        return 6;
    }

    PDFObj *root_ref = pdf_dict_get(doc.trailer, "Root");
    PDFObj *root_obj = doc_resolve(&doc, root_ref);
    if (!root_obj || root_obj->type != PDF_TYPE_DICT) {
        doc_free(&doc);
        free(in_data);
        if (error_msg) snprintf(error_msg, error_msg_size, "Root catalog not found.");
        return 7;
    }

    PDFObj *pages_ref = pdf_dict_get(root_obj, "Pages");
    PageList page_list;
    pagelist_init(&page_list);
    collect_pages(&doc, pages_ref, 0, 0, 720, 540, 0, NULL, &page_list, 0);

    if (page_list.count == 0) {
        pagelist_free(&page_list);
        doc_free(&doc);
        free(in_data);
        if (error_msg) snprintf(error_msg, error_msg_size, "No pages found in input PDF.");
        return 8;
    }

    int total_slides = (int)page_list.count;
    int slides_per_page = (config && config->slides_per_page >= 2 && config->slides_per_page <= 4) ? config->slides_per_page : 3;
    int draw_border = config ? config->draw_border : 1;
    int draw_note_lines = config ? config->draw_note_lines : 1;
    int add_cover = config ? config->add_cover : 1;
    int add_end = config ? config->add_end : 1;

    const char *course = (config && config->course_name) ? config->course_name : "";
    const char *module = (config && config->module_name) ? config->module_name : "";
    const char *author = (config && config->author_name) ? config->author_name : "";
    const char *university = (config && config->university) ? config->university : "";

    FILE *f_out = _wfopen(output_path, L"wb");
    if (!f_out) {
        pagelist_free(&page_list);
        doc_free(&doc);
        free(in_data);
        if (error_msg) snprintf(error_msg, error_msg_size, "Could not open output PDF for writing.");
        return 9;
    }

    /* Keep all original objects 1 .. orig_max_obj with their original IDs!
     * Our new objects will start at orig_max_obj + 1!
     */
    int orig_max_obj = (int)doc.count - 1;
    if (orig_max_obj < 1) orig_max_obj = 1;

    int new_obj_counter = orig_max_obj + 1;

    int *xobject_ids = (int *)malloc(total_slides * sizeof(int));
    for (int i = 0; i < total_slides; i++) {
        xobject_ids[i] = new_obj_counter++;
    }

    int font_helv = new_obj_counter++;
    int font_helv_bold = new_obj_counter++;

    int handout_pages_count = (total_slides + slides_per_page - 1) / slides_per_page;
    int total_output_pages = handout_pages_count + (add_cover ? 1 : 0) + (add_end ? 1 : 0);

    int *page_obj_ids = (int *)malloc(total_output_pages * sizeof(int));
    int *page_content_ids = (int *)malloc(total_output_pages * sizeof(int));
    for (int i = 0; i < total_output_pages; i++) {
        page_obj_ids[i] = new_obj_counter++;
        page_content_ids[i] = new_obj_counter++;
    }

    int pages_tree_id = new_obj_counter++;
    int catalog_id = new_obj_counter++;
    int total_objs = new_obj_counter - 1;

    long long *xref_offsets = (long long *)calloc(total_objs + 1, sizeof(long long));

    /* Header */
    fprintf(f_out, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");

    /* 1. Copy ALL original valid uncompressed objects so all font, image, and resource references resolve perfectly! */
    for (int i = 1; i <= orig_max_obj; i++) {
        if (i < (int)doc.count && doc.entries[i].type == 1) {
            long long off = doc.entries[i].offset;
            if (off > 0 && off < (long long)doc.size) {
                PDFParser p = { doc.data, doc.size, (size_t)off };
                char t1[64], t2[64], t3[64];
                if (parser_next_token(&p, t1, sizeof(t1)) &&
                    parser_next_token(&p, t2, sizeof(t2)) &&
                    parser_next_token(&p, t3, sizeof(t3)) &&
                    strcmp(t3, "obj") == 0) {

                    /* Find endobj */
                    const char *start_ptr = (const char *)(doc.data + off);
                    const char *end_ptr = strstr(start_ptr, "endobj");
                    if (end_ptr) {
                        size_t obj_len = (end_ptr + 6) - start_ptr;
                        xref_offsets[i] = ftell(f_out);
                        fwrite(start_ptr, 1, obj_len, f_out);
                        fprintf(f_out, "\n");
                    }
                }
            }
        }
    }

    /* 2. Write Form XObjects for each slide */
    for (int i = 0; i < total_slides; i++) {
        if (progress_cb) {
            progress_cb(i, total_slides, user_data);
        }
        PageInfo *pi = &page_list.pages[i];
        xref_offsets[xobject_ids[i]] = ftell(f_out);

        ByteBuffer raw_stream;
        bb_init(&raw_stream);
        extract_stream_decompressed(&doc, pi->contents, &raw_stream);

        uLongf comp_len = compressBound((uLong)raw_stream.size);
        unsigned char *comp_buf = (unsigned char *)malloc(comp_len);
        int zok = (comp_buf && raw_stream.size > 0) ? (compress(comp_buf, &comp_len, raw_stream.data, (uLong)raw_stream.size) == Z_OK) : 0;

        fprintf(f_out, "%d 0 obj\n", xobject_ids[i]);
        fprintf(f_out, "<<\n/Type /XObject\n/Subtype /Form\n");
        fprintf(f_out, "/BBox [%.2f %.2f %.2f %.2f]\n", pi->x0, pi->y0, pi->x1, pi->y1);

        double orig_w = pi->x1 - pi->x0;
        double orig_h = pi->y1 - pi->y0;
        if (pi->rotate == 90) {
            fprintf(f_out, "/Matrix [0 1 -1 0 %.2f 0]\n", orig_w);
        } else if (pi->rotate == 180) {
            fprintf(f_out, "/Matrix [-1 0 0 -1 %.2f %.2f]\n", orig_w, orig_h);
        } else if (pi->rotate == 270) {
            fprintf(f_out, "/Matrix [0 -1 1 0 0 %.2f]\n", orig_h);
        }

        if (pi->resources) {
            ByteBuffer res_bb;
            bb_init(&res_bb);
            serialize_obj(&res_bb, pi->resources);
            fprintf(f_out, "/Resources %.*s\n", (int)res_bb.size, (char *)res_bb.data);
            bb_free(&res_bb);
        } else {
            fprintf(f_out, "/Resources <<>>\n");
        }

        if (zok) {
            fprintf(f_out, "/Filter /FlateDecode\n/Length %lu\n>>\nstream\n", (unsigned long)comp_len);
            fwrite(comp_buf, 1, comp_len, f_out);
        } else {
            fprintf(f_out, "/Length %lu\n>>\nstream\n", (unsigned long)raw_stream.size);
            fwrite(raw_stream.data, 1, raw_stream.size, f_out);
        }
        fprintf(f_out, "\nendstream\nendobj\n");

        if (comp_buf) free(comp_buf);
        bb_free(&raw_stream);
    }

    /* 3. Write Standard Font Objects */
    xref_offsets[font_helv] = ftell(f_out);
    fprintf(f_out, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>\nendobj\n", font_helv);

    xref_offsets[font_helv_bold] = ftell(f_out);
    fprintf(f_out, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>\nendobj\n", font_helv_bold);

    /* 4. Compose Pages & Handout Layout */
    int current_page_idx = 0;

    /* A. Cover Page */
    if (add_cover) {
        int pid = page_obj_ids[current_page_idx];
        int cid = page_content_ids[current_page_idx];
        xref_offsets[pid] = ftell(f_out);

        fprintf(f_out, "%d 0 obj\n", pid);
        fprintf(f_out, "<<\n/Type /Page\n/Parent %d 0 R\n", pages_tree_id);
        fprintf(f_out, "/MediaBox [0 0 %.2f %.2f]\n", A4_WIDTH, A4_HEIGHT);
        fprintf(f_out, "/Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >>\n", font_helv, font_helv_bold);
        fprintf(f_out, "/Contents %d 0 R\n>>\nendobj\n", cid);

        ByteBuffer cov;
        bb_init(&cov);
        bb_append_str(&cov, "q\n");

        if (university && *university) {
            bb_printf(&cov, "BT /F2 14 Tf 0.2 0.2 0.2 rg 1 0 0 1 80 570 Tm (%s) Tj ET\n", university);
        }

        bb_append_str(&cov, "0.75 0.75 0.75 RG 1 w 80 545 m 515 545 l S\n");

        const char *display_course = (course && *course) ? course : ((module && *module) ? module : "STUDY HANDOUT");
        bb_printf(&cov, "BT /F2 20 Tf 0.1 0.1 0.1 rg 1 0 0 1 80 510 Tm (%s) Tj ET\n", display_course);

        if (module && *module && strcmp(display_course, module) != 0) {
            bb_printf(&cov, "BT /F1 13 Tf 0.35 0.35 0.35 rg 1 0 0 1 80 475 Tm (%s) Tj ET\n", module);
        }

        bb_append_str(&cov, "0.85 0.85 0.85 RG 0.5 w 80 440 m 515 440 l S\n");

        if (author && *author) {
            bb_printf(&cov, "BT /F2 12 Tf 0.25 0.25 0.25 rg 1 0 0 1 80 405 Tm (%s) Tj ET\n", author);
            bb_append_str(&cov, "BT /F1 10 Tf 0.5 0.5 0.5 rg 1 0 0 1 80 385 Tm (Lecture Notes & Study Handouts) Tj ET\n");
        } else {
            bb_append_str(&cov, "BT /F1 11 Tf 0.5 0.5 0.5 rg 1 0 0 1 80 405 Tm (Lecture Notes & Study Handouts) Tj ET\n");
        }

        bb_printf(&cov, "BT /F1 9 Tf 0.6 0.6 0.6 rg 1 0 0 1 80 120 Tm (Total Slides: %d | Formatted for Note-Taking & Fast Review) Tj ET\n", total_slides);
        bb_append_str(&cov, "Q\n");

        xref_offsets[cid] = ftell(f_out);
        fprintf(f_out, "%d 0 obj\n<< /Length %lu >>\nstream\n", cid, (unsigned long)cov.size);
        fwrite(cov.data, 1, cov.size, f_out);
        fprintf(f_out, "\nendstream\nendobj\n");
        bb_free(&cov);

        current_page_idx++;
    }

    /* B. Handout Slides with Note Lines */
    double slot_h = A4_HEIGHT / (double)slides_per_page;
    double slide_w = 270.0;
    double left_margin = 32.0;

    for (int p_idx = 0; p_idx < handout_pages_count; p_idx++) {
        int pid = page_obj_ids[current_page_idx];
        int cid = page_content_ids[current_page_idx];
        xref_offsets[pid] = ftell(f_out);

        int slide_start = p_idx * slides_per_page;
        int slide_end = slide_start + slides_per_page;
        if (slide_end > total_slides) slide_end = total_slides;

        fprintf(f_out, "%d 0 obj\n", pid);
        fprintf(f_out, "<<\n/Type /Page\n/Parent %d 0 R\n", pages_tree_id);
        fprintf(f_out, "/MediaBox [0 0 %.2f %.2f]\n", A4_WIDTH, A4_HEIGHT);
        fprintf(f_out, "/Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> /XObject <<\n", font_helv, font_helv_bold);
        for (int s = slide_start; s < slide_end; s++) {
            fprintf(f_out, "/S%d %d 0 R\n", s, xobject_ids[s]);
        }
        fprintf(f_out, ">> >>\n/Contents %d 0 R\n>>\nendobj\n", cid);

        ByteBuffer cs;
        bb_init(&cs);

        /* Header Watermark - Precisely Right-Aligned to Note Line Margin */
        if (author && *author) {
            double fsize = 7.0;
            double text_w = get_helvetica_text_width(author, fsize);
            double target_right_x = A4_WIDTH - 28.0; /* exactly aligns with the right note line edge */
            double text_x = target_right_x - text_w;
            double text_y = A4_HEIGHT - 22.0; /* top margin */
            bb_printf(&cs, "BT /F1 %.1f Tf 0.55 0.55 0.55 rg 1 0 0 1 %.2f %.2f Tm (%s) Tj ET\n", fsize, text_x, text_y, author);
        }

        for (int slot = 0; slot < slides_per_page; slot++) {
            int s_idx = slide_start + slot;
            if (s_idx >= total_slides) break;

            PageInfo *pi = &page_list.pages[s_idx];
            double orig_w = pi->x1 - pi->x0;
            double orig_h = pi->y1 - pi->y0;
            if (pi->rotate == 90 || pi->rotate == 270) {
                double tmp = orig_w; orig_w = orig_h; orig_h = tmp;
            }
            if (orig_w <= 0.0) orig_w = 720.0;
            if (orig_h <= 0.0) orig_h = 540.0;

            double max_h = slot_h * 0.90;
            double scale = slide_w / orig_w;
            if (orig_h * scale > max_h) {
                scale = max_h / orig_h;
            }
            double final_w = orig_w * scale;
            double final_h = orig_h * scale;

            double slot_bottom = A4_HEIGHT - (slot + 1) * slot_h;
            double pos_x = left_margin + (slide_w - final_w) / 2.0;
            double pos_y = slot_bottom + (slot_h - final_h) / 2.0;

            bb_append_str(&cs, "q\n");
            bb_printf(&cs, "%.4f 0 0 %.4f %.2f %.2f cm\n", scale, scale, pos_x - (pi->x0 * scale), pos_y - (pi->y0 * scale));
            bb_printf(&cs, "/S%d Do\n", s_idx);
            bb_append_str(&cs, "Q\n");

            if (draw_border) {
                bb_append_str(&cs, "q\n0.75 0.75 0.75 RG 0.5 w\n");
                bb_printf(&cs, "%.2f %.2f %.2f %.2f re S\n", pos_x, pos_y, final_w, final_h);
                bb_append_str(&cs, "Q\n");
            }

            if (draw_note_lines) {
                double line_x1 = left_margin + slide_w + 24.0;
                double line_x2 = A4_WIDTH - 28.0;
                int num_lines = (slides_per_page == 2) ? 7 : (slides_per_page == 3 ? 5 : 3);
                double line_step = final_h / (double)(num_lines + 1);

                bb_append_str(&cs, "q\n0.82 0.82 0.82 RG 0.5 w\n");
                for (int l = 1; l <= num_lines; l++) {
                    double ly = pos_y + l * line_step;
                    bb_printf(&cs, "%.2f %.2f m %.2f %.2f l S\n", line_x1, ly, line_x2, ly);
                }
                bb_append_str(&cs, "Q\n");
            }
        }

        xref_offsets[cid] = ftell(f_out);
        fprintf(f_out, "%d 0 obj\n<< /Length %lu >>\nstream\n", cid, (unsigned long)cs.size);
        fwrite(cs.data, 1, cs.size, f_out);
        fprintf(f_out, "\nendstream\nendobj\n");
        bb_free(&cs);

        current_page_idx++;
    }

    /* C. End Page */
    if (add_end) {
        int pid = page_obj_ids[current_page_idx];
        int cid = page_content_ids[current_page_idx];
        xref_offsets[pid] = ftell(f_out);

        fprintf(f_out, "%d 0 obj\n", pid);
        fprintf(f_out, "<<\n/Type /Page\n/Parent %d 0 R\n", pages_tree_id);
        fprintf(f_out, "/MediaBox [0 0 %.2f %.2f]\n", A4_WIDTH, A4_HEIGHT);
        fprintf(f_out, "/Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >>\n", font_helv, font_helv_bold);
        fprintf(f_out, "/Contents %d 0 R\n>>\nendobj\n", cid);

        ByteBuffer end_cs;
        bb_init(&end_cs);
        bb_append_str(&end_cs, "q\n");

        const char *display_course = (course && *course) ? course : ((module && *module) ? module : "STUDY HANDOUT");
        bb_printf(&end_cs, "BT /F2 16 Tf 0.2 0.2 0.2 rg 1 0 0 1 80 480 Tm (%s) Tj ET\n", display_course);
        bb_append_str(&end_cs, "0.75 0.75 0.75 RG 0.75 w 80 455 m 515 455 l S\n");
        bb_append_str(&end_cs, "BT /F2 12 Tf 0.3 0.3 0.3 rg 1 0 0 1 80 425 Tm (- END OF MODULE -) Tj ET\n");
        if (author && *author) {
            bb_printf(&end_cs, "BT /F1 10 Tf 0.5 0.5 0.5 rg 1 0 0 1 80 395 Tm (%s) Tj ET\n", author);
        }
        bb_append_str(&end_cs, "Q\n");

        xref_offsets[cid] = ftell(f_out);
        fprintf(f_out, "%d 0 obj\n<< /Length %lu >>\nstream\n", cid, (unsigned long)end_cs.size);
        fwrite(end_cs.data, 1, end_cs.size, f_out);
        fprintf(f_out, "\nendstream\nendobj\n");
        bb_free(&end_cs);

        current_page_idx++;
    }

    /* 5. Write Pages Tree */
    xref_offsets[pages_tree_id] = ftell(f_out);
    fprintf(f_out, "%d 0 obj\n", pages_tree_id);
    fprintf(f_out, "<<\n/Type /Pages\n/Count %d\n/Kids [", total_output_pages);
    for (int i = 0; i < total_output_pages; i++) {
        fprintf(f_out, "%d 0 R%s", page_obj_ids[i], (i + 1 < total_output_pages) ? " " : "");
    }
    fprintf(f_out, "]\n>>\nendobj\n");

    /* 6. Write Catalog */
    xref_offsets[catalog_id] = ftell(f_out);
    fprintf(f_out, "%d 0 obj\n<<\n/Type /Catalog\n/Pages %d 0 R\n>>\nendobj\n", catalog_id, pages_tree_id);

    /* 7. Write XRef Table & Trailer */
    long long xref_start = ftell(f_out);
    fprintf(f_out, "xref\n0 %d\n", total_objs + 1);
    fprintf(f_out, "0000000000 65535 f \n");
    for (int i = 1; i <= total_objs; i++) {
        if (xref_offsets[i] > 0) {
            fprintf(f_out, "%010lld 00000 n \n", xref_offsets[i]);
        } else {
            fprintf(f_out, "0000000000 65535 f \n");
        }
    }
    fprintf(f_out, "trailer\n<<\n/Size %d\n/Root %d 0 R\n>>\n", total_objs + 1, catalog_id);
    fprintf(f_out, "startxref\n%lld\n%%%%EOF\n", xref_start);

    fclose(f_out);

    if (out_pages) *out_pages = total_output_pages;
    if (out_slides) *out_slides = total_slides;

    free(xobject_ids);
    free(page_obj_ids);
    free(page_content_ids);
    free(xref_offsets);
    pagelist_free(&page_list);
    doc_free(&doc);
    free(in_data);

    return 0;
}

int process_pdf_file(
    const char *input_path,
    const char *output_path,
    const PDFEngineConfig *config,
    PDFProgressCallback progress_cb,
    void *user_data,
    int *out_pages,
    int *out_slides,
    char *error_msg,
    size_t error_msg_size
) {
    wchar_t in_w[1024] = { 0 };
    wchar_t out_w[1024] = { 0 };
    if (input_path) MultiByteToWideChar(CP_UTF8, 0, input_path, -1, in_w, 1024);
    if (output_path) MultiByteToWideChar(CP_UTF8, 0, output_path, -1, out_w, 1024);
    return process_pdf_file_w(in_w, out_w, config, progress_cb, user_data, out_pages, out_slides, error_msg, error_msg_size);
}
