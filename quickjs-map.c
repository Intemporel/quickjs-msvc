#include "quickjs-map.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QUICKJS_MAP_MAX_PATH_SEGMENTS 512

typedef struct ParsedSourceMap
{
    char sources[QUICKJS_MAP_MAX_SOURCES][QUICKJS_MAP_PATH_MAX];
    int source_count;
    char* mappings;
} ParsedSourceMap;

static int from_base_64(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int decode_vlq_value(const char* str, size_t len, size_t* idx, int* out)
{
    int result = 0;
    int shift = 0;
    int cont;

    if (str == NULL || idx == NULL || out == NULL || *idx >= len)
        return 0;

    do
    {
        int val;

        if (*idx >= len)
            return 0;

        val = from_base_64(str[*idx]);
        if (val == -1)
            return 0;

        cont = val & 32;
        val &= 31;
        result |= val << shift;
        shift += 5;
        (*idx)++;
    } while (cont);

    if (result & 1)
        *out = -(result >> 1);
    else
        *out = result >> 1;

    return 1;
}

static char* read_file(const char* path)
{
    FILE* f;
    long len;
    char* buf;
    size_t read_len;

    f = fopen(path, "rb");
    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len < 0)
    {
        fclose(f);
        return NULL;
    }

    buf = (char*)malloc((size_t)len + 1);
    if (!buf)
    {
        fclose(f);
        return NULL;
    }

    read_len = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (read_len != (size_t)len)
    {
        free(buf);
        return NULL;
    }

    buf[len] = '\0';
    return buf;
}

static void copy_string(char* dest, size_t dest_size, const char* src)
{
    if (dest_size == 0)
        return;

    if (src == NULL)
    {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static const char* skip_json_ws(const char* p)
{
    while (*p != '\0' && isspace((unsigned char)*p))
        p++;

    return p;
}

static int append_char(char** buffer, size_t* capacity, size_t* length, char c)
{
    char* grown;
    size_t new_capacity;

    if (*length + 1 >= *capacity)
    {
        new_capacity = (*capacity == 0) ? 64 : (*capacity * 2);
        grown = (char*)realloc(*buffer, new_capacity);
        if (!grown)
            return 0;

        *buffer = grown;
        *capacity = new_capacity;
    }

    (*buffer)[(*length)++] = c;
    return 1;
}

static int hex_digit_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int json_skip_string(const char** pp)
{
    const char* p;

    if (pp == NULL || *pp == NULL || **pp != '"')
        return 0;

    p = *pp + 1;
    while (*p != '\0')
    {
        if (*p == '\\')
        {
            p++;
            if (*p == '\0')
                return 0;

            if (*p == 'u')
            {
                int i;
                p++;
                for (i = 0; i < 4; i++, p++)
                {
                    if (hex_digit_value(*p) < 0)
                        return 0;
                }
            }
            else
            {
                p++;
            }
            continue;
        }

        if (*p == '"')
        {
            *pp = p + 1;
            return 1;
        }

        p++;
    }

    return 0;
}

static int json_read_string_alloc(const char** pp, char** out)
{
    const char* p;
    char* buffer;
    size_t capacity = 64;
    size_t length = 0;

    if (pp == NULL || *pp == NULL || out == NULL || **pp != '"')
        return 0;

    buffer = (char*)malloc(capacity);
    if (!buffer)
        return 0;

    p = *pp + 1;
    while (*p != '\0')
    {
        char c = *p++;

        if (c == '"')
        {
            if (!append_char(&buffer, &capacity, &length, '\0'))
            {
                free(buffer);
                return 0;
            }

            *out = buffer;
            *pp = p;
            return 1;
        }

        if (c == '\\')
        {
            int codepoint;
            int digit;

            c = *p++;
            if (c == '\0')
                break;

            switch (c)
            {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'b':
                    c = '\b';
                    break;
                case 'f':
                    c = '\f';
                    break;
                case 'n':
                    c = '\n';
                    break;
                case 'r':
                    c = '\r';
                    break;
                case 't':
                    c = '\t';
                    break;
                case 'u':
                    codepoint = 0;
                    digit = hex_digit_value(*p++);
                    if (digit < 0)
                        goto fail;
                    codepoint = (codepoint << 4) | digit;
                    digit = hex_digit_value(*p++);
                    if (digit < 0)
                        goto fail;
                    codepoint = (codepoint << 4) | digit;
                    digit = hex_digit_value(*p++);
                    if (digit < 0)
                        goto fail;
                    codepoint = (codepoint << 4) | digit;
                    digit = hex_digit_value(*p++);
                    if (digit < 0)
                        goto fail;
                    codepoint = (codepoint << 4) | digit;
                    c = (codepoint >= 0 && codepoint < 0x80) ? (char)codepoint : '?';
                    break;
                default:
                    goto fail;
            }
        }

        if (!append_char(&buffer, &capacity, &length, c))
            goto fail;
    }

fail:
    free(buffer);
    return 0;
}

static int json_skip_value(const char** pp);

static int json_skip_array(const char** pp)
{
    const char* p;

    if (pp == NULL || *pp == NULL || **pp != '[')
        return 0;

    p = *pp + 1;
    p = skip_json_ws(p);
    if (*p == ']')
    {
        *pp = p + 1;
        return 1;
    }

    for (;;)
    {
        p = skip_json_ws(p);
        if (!json_skip_value(&p))
            return 0;

        p = skip_json_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }

        if (*p == ']')
        {
            *pp = p + 1;
            return 1;
        }

        return 0;
    }
}

static int json_skip_object(const char** pp)
{
    const char* p;

    if (pp == NULL || *pp == NULL || **pp != '{')
        return 0;

    p = *pp + 1;
    p = skip_json_ws(p);
    if (*p == '}')
    {
        *pp = p + 1;
        return 1;
    }

    for (;;)
    {
        p = skip_json_ws(p);
        if (!json_skip_string(&p))
            return 0;

        p = skip_json_ws(p);
        if (*p != ':')
            return 0;

        p++;
        p = skip_json_ws(p);
        if (!json_skip_value(&p))
            return 0;

        p = skip_json_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }

        if (*p == '}')
        {
            *pp = p + 1;
            return 1;
        }

        return 0;
    }
}

static int json_skip_primitive(const char** pp)
{
    const char* p;

    if (pp == NULL || *pp == NULL)
        return 0;

    p = *pp;
    while (*p != '\0' &&
           *p != ',' &&
           *p != ']' &&
           *p != '}' &&
           !isspace((unsigned char)*p))
    {
        p++;
    }

    if (p == *pp)
        return 0;

    *pp = p;
    return 1;
}

static int json_skip_value(const char** pp)
{
    const char* p;

    if (pp == NULL || *pp == NULL)
        return 0;

    p = skip_json_ws(*pp);
    if (*p == '"')
    {
        *pp = p;
        return json_skip_string(pp);
    }

    if (*p == '{')
    {
        *pp = p;
        return json_skip_object(pp);
    }

    if (*p == '[')
    {
        *pp = p;
        return json_skip_array(pp);
    }

    *pp = p;
    return json_skip_primitive(pp);
}

static const char* json_find_top_level_key(const char* json, const char* key)
{
    const char* p;

    if (json == NULL || key == NULL)
        return NULL;

    p = skip_json_ws(json);
    if (*p != '{')
        return NULL;

    p++;
    for (;;)
    {
        char* name = NULL;

        p = skip_json_ws(p);
        if (*p == '}')
            return NULL;

        if (!json_read_string_alloc(&p, &name))
            return NULL;

        p = skip_json_ws(p);
        if (*p != ':')
        {
            free(name);
            return NULL;
        }

        p++;
        p = skip_json_ws(p);
        if (strcmp(name, key) == 0)
        {
            free(name);
            return p;
        }

        free(name);
        if (!json_skip_value(&p))
            return NULL;

        p = skip_json_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }

        if (*p == '}')
            return NULL;

        return NULL;
    }
}

static void free_parsed_source_map(ParsedSourceMap* map)
{
    if (map == NULL)
        return;

    free(map->mappings);
    map->mappings = NULL;
}

static int parse_map(const char* json, ParsedSourceMap* map)
{
    const char* p;

    if (json == NULL || map == NULL)
        return 0;

    memset(map, 0, sizeof(*map));

    p = json_find_top_level_key(json, "sources");
    if (p == NULL || *p != '[')
        return 0;

    p++;
    for (;;)
    {
        char* source = NULL;

        p = skip_json_ws(p);
        if (*p == ']')
        {
            p++;
            break;
        }

        if (map->source_count >= QUICKJS_MAP_MAX_SOURCES)
            return 0;

        if (!json_read_string_alloc(&p, &source))
            return 0;

        copy_string(map->sources[map->source_count], sizeof(map->sources[0]), source);
        free(source);
        map->source_count++;

        p = skip_json_ws(p);
        if (*p == ',')
        {
            p++;
            continue;
        }

        if (*p == ']')
        {
            p++;
            break;
        }

        return 0;
    }

    p = json_find_top_level_key(json, "mappings");
    if (p == NULL)
        return 0;

    if (!json_read_string_alloc(&p, &map->mappings))
    {
        free_parsed_source_map(map);
        return 0;
    }

    return map->source_count > 0 && map->mappings != NULL;
}

static int parse_segment(const char* segment,
                         size_t segment_len,
                         int* generated_column_delta,
                         int* has_source,
                         int* source_delta,
                         int* original_line_delta,
                         int* original_column_delta)
{
    size_t idx = 0;
    int ignored_name_delta;

    if (segment == NULL || generated_column_delta == NULL || has_source == NULL ||
        source_delta == NULL || original_line_delta == NULL || original_column_delta == NULL)
    {
        return 0;
    }

    *has_source = 0;
    *source_delta = 0;
    *original_line_delta = 0;
    *original_column_delta = 0;

    if (!decode_vlq_value(segment, segment_len, &idx, generated_column_delta))
        return 0;

    if (idx == segment_len)
        return 1;

    if (!decode_vlq_value(segment, segment_len, &idx, source_delta) ||
        !decode_vlq_value(segment, segment_len, &idx, original_line_delta) ||
        !decode_vlq_value(segment, segment_len, &idx, original_column_delta))
    {
        return 0;
    }

    *has_source = 1;
    if (idx == segment_len)
        return 1;

    if (!decode_vlq_value(segment, segment_len, &idx, &ignored_name_delta))
        return 0;

    return idx == segment_len;
}

static int is_path_separator(char c)
{
    return c == '/' || c == '\\';
}

static void normalize_path_separators(char* path)
{
    if (path == NULL)
        return;

    while (*path != '\0')
    {
        if (*path == '\\')
            *path = '/';
        path++;
    }
}

static int is_absolute_path(const char* path)
{
    if (path == NULL || path[0] == '\0')
        return 0;

    if (is_path_separator(path[0]))
        return 1;

    return isalpha((unsigned char)path[0]) && path[1] == ':' && is_path_separator(path[2]);
}

static int file_exists(const char* path)
{
    FILE* f;

    if (path == NULL || path[0] == '\0')
        return 0;

    f = fopen(path, "rb");
    if (!f)
        return 0;

    fclose(f);
    return 1;
}

static void get_directory_name(const char* path, char* out, size_t out_size)
{
    char* last_separator;

    copy_string(out, out_size, path);
    normalize_path_separators(out);

    last_separator = strrchr(out, '/');
    if (last_separator == NULL)
    {
        copy_string(out, out_size, ".");
        return;
    }

    if (last_separator == out)
    {
        out[1] = '\0';
        return;
    }

    if (last_separator == out + 2 && out[1] == ':')
    {
        out[3] = '\0';
        return;
    }

    *last_separator = '\0';
}

static void collapse_path(char* path)
{
    char temp[QUICKJS_MAP_PATH_MAX];
    char prefix[8] = { 0 };
    char* segments[QUICKJS_MAP_MAX_PATH_SEGMENTS];
    char* cursor;
    size_t prefix_len = 0;
    size_t segment_count = 0;
    int absolute = 0;
    size_t i;

    if (path == NULL || path[0] == '\0')
        return;

    copy_string(temp, sizeof(temp), path);
    normalize_path_separators(temp);
    cursor = temp;

    if (isalpha((unsigned char)cursor[0]) && cursor[1] == ':')
    {
        prefix[prefix_len++] = cursor[0];
        prefix[prefix_len++] = ':';
        cursor += 2;
        if (*cursor == '/')
        {
            prefix[prefix_len++] = '/';
            cursor++;
            absolute = 1;
        }
    }
    else if (cursor[0] == '/' && cursor[1] == '/')
    {
        prefix[prefix_len++] = '/';
        prefix[prefix_len++] = '/';
        cursor += 2;
        absolute = 1;
    }
    else if (cursor[0] == '/')
    {
        prefix[prefix_len++] = '/';
        cursor++;
        absolute = 1;
    }

    prefix[prefix_len] = '\0';

    while (*cursor != '\0')
    {
        char* segment_start;

        while (*cursor == '/')
            cursor++;

        if (*cursor == '\0')
            break;

        segment_start = cursor;
        while (*cursor != '\0' && *cursor != '/')
            cursor++;

        if (*cursor != '\0')
            *cursor++ = '\0';

        if (strcmp(segment_start, ".") == 0 || segment_start[0] == '\0')
            continue;

        if (strcmp(segment_start, "..") == 0)
        {
            if (segment_count > 0 && strcmp(segments[segment_count - 1], "..") != 0)
            {
                segment_count--;
            }
            else if (!absolute && segment_count < QUICKJS_MAP_MAX_PATH_SEGMENTS)
            {
                segments[segment_count++] = segment_start;
            }
            continue;
        }

        if (segment_count < QUICKJS_MAP_MAX_PATH_SEGMENTS)
            segments[segment_count++] = segment_start;
    }

    copy_string(path, QUICKJS_MAP_PATH_MAX, prefix);
    for (i = 0; i < segment_count; i++)
    {
        size_t current_len = strlen(path);

        if (current_len != 0 && path[current_len - 1] != '/')
        {
            if (current_len + 1 < QUICKJS_MAP_PATH_MAX)
            {
                path[current_len] = '/';
                path[current_len + 1] = '\0';
            }
        }

        strncat(path, segments[i], QUICKJS_MAP_PATH_MAX - strlen(path) - 1);
    }

    if (path[0] == '\0')
    {
        if (absolute)
            copy_string(path, QUICKJS_MAP_PATH_MAX, prefix_len != 0 ? prefix : "/");
        else
            copy_string(path, QUICKJS_MAP_PATH_MAX, ".");
    }
}

static void resolve_source_path(const char* map_file, const char* source, char* out, size_t out_size)
{
    char candidate[QUICKJS_MAP_PATH_MAX];
    char directory[QUICKJS_MAP_PATH_MAX];

    if (source == NULL)
    {
        if (out_size > 0)
            out[0] = '\0';
        return;
    }

    if (is_absolute_path(source))
    {
        copy_string(candidate, sizeof(candidate), source);
    }
    else
    {
        get_directory_name(map_file, directory, sizeof(directory));
        if (strcmp(directory, ".") == 0)
            snprintf(candidate, sizeof(candidate), "%s", source);
        else
            snprintf(candidate, sizeof(candidate), "%s/%s", directory, source);
    }

    normalize_path_separators(candidate);
    collapse_path(candidate);
    if (file_exists(candidate))
    {
        copy_string(out, out_size, candidate);
        return;
    }

    copy_string(out, out_size, source);
    normalize_path_separators(out);
}

int map_javascript_to_typescript(const char* jsFile, int jsLine, int jsCol, char* tsFile, int* tsLine, int* tsCol)
{
    char* jsMapFile;
    char* buf;
    ParsedSourceMap map;
    const char* p;
    int target_line;
    int target_col;
    int current_line = 0;
    int previous_source = 0;
    int previous_original_line = 0;
    int previous_original_col = 0;
    size_t jsFileLen;

    if (jsFile == NULL || tsFile == NULL || tsLine == NULL || tsCol == NULL || jsLine <= 0)
        return 0;

    tsFile[0] = '\0';
    *tsLine = 0;
    *tsCol = 0;

    jsFileLen = strlen(jsFile);
    jsMapFile = (char*)malloc(jsFileLen + 5);
    if (!jsMapFile)
        return 0;

    snprintf(jsMapFile, jsFileLen + 5, "%s.map", jsFile);
    buf = read_file(jsMapFile);
    if (!buf)
    {
        free(jsMapFile);
        return 0;
    }

    if (!parse_map(buf, &map))
    {
        free(buf);
        free(jsMapFile);
        return 0;
    }
    free(buf);

    target_line = jsLine - 1;
    target_col = (jsCol > 0) ? (jsCol - 1) : 0;
    p = map.mappings;

    while (1)
    {
        int generated_column = 0;

        if (current_line > target_line)
            break;

        if (*p == '\0' && current_line < target_line)
            break;

        while (*p != '\0' && *p != ';')
        {
            const char* segment_start = p;
            size_t segment_len;

            while (*p != '\0' && *p != ',' && *p != ';')
                p++;

            segment_len = (size_t)(p - segment_start);
            if (segment_len > 0)
            {
                int generated_column_delta;
                int has_source;
                int source_delta;
                int original_line_delta;
                int original_column_delta;

                if (!parse_segment(segment_start, segment_len,
                                   &generated_column_delta,
                                   &has_source,
                                   &source_delta,
                                   &original_line_delta,
                                   &original_column_delta))
                {
                    free_parsed_source_map(&map);
                    free(jsMapFile);
                    return 0;
                }

                generated_column += generated_column_delta;
                if (has_source)
                {
                    previous_source += source_delta;
                    previous_original_line += original_line_delta;
                    previous_original_col += original_column_delta;
                }

                if (current_line == target_line && generated_column <= target_col)
                {
                    if (has_source)
                    {
                        if (previous_source < 0 || previous_source >= map.source_count)
                        {
                            free_parsed_source_map(&map);
                            free(jsMapFile);
                            return 0;
                        }

                        resolve_source_path(jsMapFile,
                                            map.sources[previous_source],
                                            tsFile,
                                            QUICKJS_MAP_PATH_MAX);
                        *tsLine = previous_original_line + 1;
                        *tsCol = previous_original_col + 1;
                    }
                    else
                    {
                        tsFile[0] = '\0';
                    }
                }
            }

            if (*p == ',')
                p++;
        }

        if (current_line == target_line)
        {
            free_parsed_source_map(&map);
            free(jsMapFile);
            return tsFile[0] != '\0';
        }

        if (*p == ';')
        {
            p++;
            current_line++;
            continue;
        }

        break;
    }

    free_parsed_source_map(&map);
    free(jsMapFile);
    return 0;
}
