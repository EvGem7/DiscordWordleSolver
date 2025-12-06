#include <stdio.h>

#include "solver.c"

int test_wordle(Word wordle) {
    reset_probes();
    while (get_probes_count() < MAX_PROBES) {
        Word guess = guess_word();
        if (Word_equals(wordle, guess)) {
            return get_probes_count() + 1;
        }
        Word result = generate_result(guess, wordle);
        save_probe((Probe) { .guess = guess, .result = result });
    }
    return -1;
}

int test_solver_printf(const char *restrict format, ...) {
    (void)format;
    return 0;
}

int main(void) {
    solver_printf = test_solver_printf;
    bool were_errors = false;

    puts("[");
    for (size_t i = 0; i < WORDS_COUNT; i++) {
        Word wordle = WORDS[i];
        int probes = test_wordle(wordle);
        if (probes <= 0) {
            were_errors = true;
        }
        bool is_last = i == WORDS_COUNT - 1;
        printf("    { \"wordle\": \"%.5s\", \"probes\": %d }%s\n", wordle.val, probes, is_last ? "" : ",");
    }
    puts("]");

    if (were_errors) {
        fprintf(stderr, "Some tests failed!\n");
        return -1;
    }
    return 0;
}

