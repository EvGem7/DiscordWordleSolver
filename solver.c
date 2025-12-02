#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "solver.h"

//#define DEBUG

static Word words[MAX_WORDS] = {0};
static uint32_t words_count = 0;

static Probe probes[MAX_PROBES] = {0};
static uint32_t probes_count = 0;


bool Word_equals(Word a, Word b) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (a.val[i] != b.val[i]) return false;
    }
    return true;
}


typedef uint64_t CharSet;

bool CharSet_contains(CharSet set, char ch) {
    return (set & (1 << (ch - 'a'))) != 0;
}

bool CharSet_is_empty(CharSet set) {
    return set == 0;
}

void CharSet_add(CharSet *set, char ch) {
    *set |= (1 << (ch - 'a'));
}


typedef struct {
    CharSet sets[WORD_LEN];
} CharSetArray;


typedef struct {
    Word *arr;
    size_t size;
} WordArray;

typedef struct {
    Probe *arr;
    size_t size;
} ProbeArray;

typedef struct {
    Word word;
    int amount;
} WordAmount;


CharSet get_char_set(const Color color, const ProbeArray probes) {
    CharSet result = {0};
    for (Probe *probe = probes.arr; probe < probes.arr + probes.size; probe++) {
        for (int i = 0; i < WORD_LEN; i++) {
            if (probe->result.val[i] == color) {
                CharSet_add(&result, probe->guess.val[i]);
            }
        }
    }
    return result;
}

CharSetArray get_char_set_array(const Color color, const ProbeArray probes) {
    CharSetArray result = {0};
    for (Probe *probe = probes.arr; probe < probes.arr + probes.size; probe++) {
        for (int i = 0; i < WORD_LEN; i++) {
            if (probe->result.val[i] == color) {
                CharSet_add(result.sets + i, probe->guess.val[i]);
            }
        }
    }
    return result;
}

Word generate_result(const Word guess, const Word actual) {
    Word result = {0};
    memset(result.val, GRAY, WORD_LEN);
    for (int result_i = 0; result_i < WORD_LEN; result_i++) {
        if (guess.val[result_i] == actual.val[result_i]) {
            result.val[result_i] = GREEN;
            continue;
        }
        for (int actual_i = 0; actual_i < WORD_LEN; actual_i++) {
            if (guess.val[result_i] == actual.val[actual_i] && guess.val[actual_i] != actual.val[actual_i]) {
                result.val[result_i] = YELLOW;
            }
        }
    }
    return result;
}

size_t filter_words(Word *dst, const WordArray words, const ProbeArray probes) {
    const CharSetArray gray_arr = get_char_set_array(GRAY, probes);
    const CharSetArray yellow_arr = get_char_set_array(YELLOW, probes);
    const CharSetArray green_arr = get_char_set_array(GREEN, probes);

    const CharSet gray_set = get_char_set(GRAY, probes);
    const CharSet yellow_set = get_char_set(YELLOW, probes);
    const CharSet green_set = get_char_set(GREEN, probes);

    size_t count = 0;
    for (Word *w = words.arr; w < words.arr + words.size; w++) {
        bool matches = true;
        for (int i = 0; i < WORD_LEN; i++) {
            char ch = w->val[i];
            if (CharSet_contains(gray_set, ch) &&
                !CharSet_contains(green_set, ch) &&
                !CharSet_contains(yellow_set, ch))
            {
                matches = false;
                break;
            }
            if (CharSet_contains(yellow_arr.sets[i], ch) || CharSet_contains(gray_arr.sets[i], ch)) {
                matches = false;
                break;
            }
            if (!CharSet_is_empty(green_arr.sets[i] && !CharSet_contains(green_arr.sets[i], ch))) {
                matches = false;
                break;
            }
        }
        if (matches) {
            dst[count++] = *w;
        }
    }
    return count;
}

size_t count_filtered_words(const WordArray words, const ProbeArray probes, Word ignore) {
    const CharSetArray gray_arr = get_char_set_array(GRAY, probes);
    const CharSetArray yellow_arr = get_char_set_array(YELLOW, probes);
    const CharSetArray green_arr = get_char_set_array(GREEN, probes);

    const CharSet gray_set = get_char_set(GRAY, probes);
    const CharSet yellow_set = get_char_set(YELLOW, probes);
    const CharSet green_set = get_char_set(GREEN, probes);

    size_t count = 0;
    for (Word *w = words.arr; w < words.arr + words.size; w++) {
        if (Word_equals(*w, ignore)) continue;

        bool matches = true;
        for (int i = 0; i < WORD_LEN; i++) {
            char ch = w->val[i];
            if (CharSet_contains(gray_set, ch) &&
                !CharSet_contains(green_set, ch) &&
                !CharSet_contains(yellow_set, ch)) 
            {
                matches = false;
                break;
            }
            if (CharSet_contains(yellow_arr.sets[i], ch) || CharSet_contains(gray_arr.sets[i], ch)) {
                matches = false;
                break;
            }
            if (!CharSet_is_empty(green_arr.sets[i] && !CharSet_contains(green_arr.sets[i], ch))) {
                matches = false;
                break;
            }
        }
        if (matches) {
            count++;
        }
    }
    return count;
}

size_t calc_possible_count(const WordArray words, Word guess, Word actual) {
    Word result = generate_result(guess, actual);
    Probe probe = { .guess = guess, .result = result };
    ProbeArray probes = { .arr = &probe, .size = 1 };
    return count_filtered_words(words, probes, guess);
}

int compare_word_amount(const void* a, const void* b) {
    const WordAmount *wa = a;
    const WordAmount *wb = b;
//    if (wa->amount == 0 && wb->amount == 0) return 0;
//    if (wa->amount == 0) return +1;
//    if (wb->amount == 0) return -1;
    return wa->amount - wb->amount;
}

Word guess_word(void) {
    WordAmount guesses[MAX_WORDS] = {0};

    Word possible_actuals[MAX_WORDS] = {0};
    const WordArray all_words = { .arr = words, .size = words_count };
    const ProbeArray all_probes = { .arr = probes, .size = probes_count };
    const size_t filtered_count  = filter_words(possible_actuals, all_words, all_probes);

//#ifdef DEBUG
    printf("Possible words: %zu\n", filtered_count);
    if (filtered_count < 100) {
        for (size_t i = 0; i < filtered_count; i++) {
            printf("%.*s ", WORD_LEN, possible_actuals[i].val);
        }
        puts("");
    }
//#endif

    const size_t pa_count = (filtered_count > 600) ? 600 : filtered_count;

    for (size_t guess_i = 0; guess_i < words_count; guess_i++) {
        const Word guess = words[guess_i];
        size_t possible_count = 0;
        for (Word* pa = possible_actuals; pa < possible_actuals + pa_count; pa++) {
            const WordArray arr = { .arr = possible_actuals, .size = pa_count };
            possible_count += calc_possible_count(arr, guess, *pa);
        }
        guesses[guess_i] = (WordAmount) { .word = guess, .amount = possible_count };
    }

    qsort(guesses, words_count, sizeof(guesses[0]), compare_word_amount);

#ifdef DEBUG
    printf("calculated for guess = %.5s possible_count = %d\n", guesses[0].word.val, guesses[0].amount);
#endif

    return guesses[0].word;
}


// not interesting bullshit
bool is_word_valid(Word word) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (word.val[i] < 'a' || word.val[i] > 'z') {
            return false;
        }
    }
    return true;
}

bool is_result_valid(Word result) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (result.val[i] != GRAY && result.val[i] != YELLOW && result.val[i] != GREEN) {
            return false;
        }
    }
    return true;
}

void save_word(Word word) {
    words[words_count++] = word;
}

void save_probe(Probe probe) {
    probes[probes_count++] = probe;
}

int get_words_count(void) {
    return words_count;
}

int get_probes_count(void) {
    return probes_count;
}

