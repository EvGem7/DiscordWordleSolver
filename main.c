#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "solver.h"


bool read_words(void) {
    FILE *file = fopen("words.txt", "rb");
    if (NULL == file) {
        return false;
    }

    uint32_t word_size = 0;
    Word word = {0};
    for (int ch = fgetc(file); ch != EOF; ch = fgetc(file)) {
        if ('\n' == ch) {
            if (WORD_LEN == word_size && is_word_valid(word)) {
                save_word(word);
            }
            word_size = 0;
            continue;
        }
        if (word_size < WORD_LEN) {
            word.val[word_size] = (char)ch;
        }
        word_size++;
    }

    return fclose(file) == 0;
}

int main(void) {
    if (!read_words()) {
        fprintf(stderr, "cannot read words\n");
        return -1;
    }
    printf("%"PRIu32" words were read\n", get_words_count());

    int probe_num = 1;
    while (get_probes_count() < MAX_PROBES) {
        Word guess = guess_word();
        printf("Probe #%d:\n%.*s\n", probe_num++, WORD_LEN, guess.val);

        while (true) {
            char input[WORD_LEN + 2];
            if (NULL == fgets(input, sizeof(input), stdin)) {
                return 0;
            }
            Word result = *(Word*)input;
            if (is_result_valid(result)) {
                Probe probe = { .guess = guess, .result = result };
                save_probe(probe);
                break;
            } else {
                printf("expected result: 1 - gray, 2 - yellow, 3 - green\n");
            }
        }
    }
    puts("Too many probes!");
    return -1;
}

