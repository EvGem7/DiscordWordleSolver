#include <stdlib.h>

#include "solver.h"

Word words[MAX_WORDS] = {0};
uint32_t words_count = 0;

Probe probes[MAX_PROBES] = {0};
uint32_t probes_count = 0;

bool is_word_valid(Word word) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (word.val[i] < 'a' || word.val[i] > 'z') {
            return false;
        }
    }
    return true;
}

bool is_result_valid(const char *result) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (result[i] != GRAY && result[i] != YELLOW && result[i] != GREEN) {
            return false;
        }
    }
    return true;
}

Word guess_word(void) {
    return words[rand() % words_count];
}

