#include "jtty.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *f = fopen("tests/wsjtx_pack.tsv", "r");
    assert(f);
    char line[512];
    int  cases = 0;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#')
            continue;
        char *columns[8];
        columns[0] = line;
        int ncols = 1;
        for (char *p = line; *p; p++)
            if (*p == '\t') {
                *p = 0;
                assert(ncols < 8);
                columns[ncols++] = p + 1;
            }
        assert(ncols == 8);
        columns[7][strcspn(columns[7], "\r\n")] = 0;
        int      profile = atoi(columns[0]), expected = atoi(columns[1]);
        uint64_t words[16];
        char     normalized[81], rendered[81];
        int      n = jtty_pack(columns[6], profile, words, normalized);
        if (n != expected)
            fprintf(stderr, "Reference mismatch for '%s': frames %d != %d\n", columns[6], n, expected);
        assert(n == expected);
        if (columns[7][0])
            assert(!strcmp(normalized, columns[7]));
        if (n) {
            assert(!jtty_unpack(words, (size_t) n, rendered) && !strcmp(normalized, rendered));
            for (int i = 0; i < n && i < 2; i++) {
                int type = atoi(columns[2 + i * 2]), subtype = atoi(columns[3 + i * 2]);
                if (type >= 0)
                    assert((int) ((words[i] >> 2) & 3) == type);
                if (subtype >= 0)
                    assert((int) ((words[i] >> 4) & 3) == subtype);
            }
        }
        cases++;
    }
    assert(!ferror(f));
    fclose(f);
    printf("WSJT-X reference packing cases passed: %d\n", cases);
    return 0;
}
