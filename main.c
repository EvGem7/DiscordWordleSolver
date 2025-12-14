#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#include "solver.h"


int main(void) {
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

