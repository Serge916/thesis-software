#include "helper.h"

void sleep_ms(int milliseconds)
{
    // Convert milliseconds to microseconds
    usleep(milliseconds * 1000);
}

int hexchar_to_uint8(char c, uint8_t *out)
{
    if ('0' <= c && c <= '9')
    {
        *out = (uint8_t)(c - '0');
        return 0;
    }
    c = (char)tolower((unsigned char)c);
    if ('a' <= c && c <= 'f')
    {
        *out = (uint8_t)(10 + (c - 'a'));
        return 0;
    }
    return -1;
}

/* ASCII -> nibble lookup (0..15), 0xFF invalid */
static const uint8_t HEX_LUT[256] = {
    [0 ... 255] = 0xFF,
    ['0'] = 0,
    ['1'] = 1,
    ['2'] = 2,
    ['3'] = 3,
    ['4'] = 4,
    ['5'] = 5,
    ['6'] = 6,
    ['7'] = 7,
    ['8'] = 8,
    ['9'] = 9,
    ['a'] = 10,
    ['b'] = 11,
    ['c'] = 12,
    ['d'] = 13,
    ['e'] = 14,
    ['f'] = 15,
    ['A'] = 10,
    ['B'] = 11,
    ['C'] = 12,
    ['D'] = 13,
    ['E'] = 14,
    ['F'] = 15,
};

int parseLine(const char s[HEXCHARS_PER_LINE], uint8_t out[BYTES_PER_LINE])
{
    for (int i = 0; i < BYTES_PER_LINE; i++)
    {
        uint8_t hi = HEX_LUT[(unsigned char)s[2 * i]];
        uint8_t lo = HEX_LUT[(unsigned char)s[2 * i + 1]];
        if (hi == 0xFF || lo == 0xFF)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 0;
}

int readNextLine(FILE *f, char hex16[HEXCHARS_PER_LINE], uint64_t *lineno)
{
    char line[256];

    while (fgets(line, sizeof(line), f))
    {
        (*lineno)++;

        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++; // skip leading WS
        if (*p == '\0' || *p == '\n' || *p == '\r')
            continue; // blank
        if (*p == '#')
            continue; // comment

        // Must have at least 16 chars before newline/CR/end
        for (int i = 0; i < HEXCHARS_PER_LINE; i++)
        {
            char c = p[i];
            if (c == '\0' || c == '\n' || c == '\r')
            {
                fprintf(stderr, "Line %d: too short (need 16 hex chars)\n", *lineno);
                return -1;
            }
            hex16[i] = c;
        }

        // Optional: allow trailing whitespace and/or trailing comment
        char *q = p + HEXCHARS_PER_LINE;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q != '\0' && *q != '\n' && *q != '\r' && *q != '#')
        {
            fprintf(stderr, "Line %d: extra garbage after 16 hex chars\n", *lineno);
            return -1;
        }

        return 1; // success
    }

    return 0; // EOF
}