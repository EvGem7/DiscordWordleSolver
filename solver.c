#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>

#include "solver.h"
#include "words.c"

static Probe PROBES[MAX_PROBES] = {0};
static uint32_t PROBES_COUNT = 0;


typedef uint64_t CharSet;
static bool CharSet_contains(CharSet set, char ch);
static bool CharSet_is_empty(CharSet set);
static void CharSet_add(CharSet *set, char ch);
static void CharSet_remove(CharSet *set, char ch);


static uint64_t count_cache[WORDS_COUNT][243]; // 243 = 3 ** 5; 3 stands for 3 possible result colors

static int get_cached_count_index(Word result);
static uint64_t get_cached_count(size_t guess_index, Word result);
static void put_cached_count(size_t guess_index, Word result, uint64_t count);
static void clear_cache(void);

#define CACHE_MISS (~(uint64_t)(0))


typedef struct {
    CharSet sets[WORD_LEN];
} CharSetArray;

typedef struct {
    const Word *arr;
    size_t size;
} WordArray;

typedef struct {
    const Probe *arr;
    size_t size;
} ProbeArray;

typedef struct {
    Word word;
    int amount;
} WordAmount;

typedef struct {
    CharSet gray_set;
    CharSet yellow_set;
    CharSet green_set;
    CharSetArray gray_arr;
    CharSetArray yellow_arr;
    CharSetArray green_arr;
} ColorInfo;



static CharSet get_char_set(Color color, ProbeArray probes) {
    CharSet result = {0};
    for (const Probe *probe = probes.arr; probe < probes.arr + probes.size; probe++) {
        for (int i = 0; i < WORD_LEN; i++) {
            if (probe->result.val[i] == color) {
                CharSet_add(&result, probe->guess.val[i]);
            }
        }
    }
    return result;
}

static CharSetArray get_char_set_array(Color color, ProbeArray probes) {
    CharSetArray result = {0};
    for (const Probe *probe = probes.arr; probe < probes.arr + probes.size; probe++) {
        for (int i = 0; i < WORD_LEN; i++) {
            if (probe->result.val[i] == color) {
                CharSet_add(result.sets + i, probe->guess.val[i]);
            }
        }
    }
    return result;
}

static Word generate_result(Word guess, Word actual) {
    Word result = {0};
    memset(result.val, GRAY, WORD_LEN);

    Word yellow_check = actual;
    for (int result_i = 0; result_i < WORD_LEN; result_i++) {
        if (guess.val[result_i] == actual.val[result_i]) {
            result.val[result_i] = GREEN;
            continue;
        }
        for (int yc_i = 0; yc_i < WORD_LEN; yc_i++) {
            if (guess.val[result_i] == yellow_check.val[yc_i] && guess.val[yc_i] != yellow_check.val[yc_i]) {
                result.val[result_i] = YELLOW;
                yellow_check.val[yc_i] = 0;
                break;
            }
        }
    }

    return result;
}

static bool word_matches(Word word, const ColorInfo *info) {
    CharSet yellow_covered = info->yellow_set;
    for (int i = 0; i < WORD_LEN; i++) {
        char ch = word.val[i];
        if (CharSet_contains(info->gray_set, ch) &&
            !CharSet_contains(info->green_set, ch) &&
            !CharSet_contains(info->yellow_set, ch)) 
        {
            return false;
        }
        if (CharSet_contains(info->yellow_arr.sets[i], ch) || CharSet_contains(info->gray_arr.sets[i], ch)) {
            return false;
        }
        if (!CharSet_is_empty(info->green_arr.sets[i] && !CharSet_contains(info->green_arr.sets[i], ch))) {
            return false;
        }
        // TODO: impove filtering by yellow
        CharSet_remove(&yellow_covered, ch);
    }
    return CharSet_is_empty(yellow_covered);
}

static ColorInfo get_color_info(ProbeArray probes) {
    return (ColorInfo) {
        .gray_arr = get_char_set_array(GRAY, probes),
        .yellow_arr = get_char_set_array(YELLOW, probes),
        .green_arr = get_char_set_array(GREEN, probes),
        .gray_set = get_char_set(GRAY, probes),
        .yellow_set = get_char_set(YELLOW, probes),
        .green_set = get_char_set(GREEN, probes),
    };
}

static size_t filter_words(Word *restrict dst, WordArray words, ProbeArray probes) {
    ColorInfo color_info = get_color_info(probes);
    size_t count = 0;
    for (const Word *w = words.arr; w < words.arr + words.size; w++) {
        if (word_matches(*w, &color_info)) {
            dst[count++] = *w;
        }
    }
    return count;
}

static size_t count_filtered_words(WordArray words, ProbeArray probes) {
    ColorInfo color_info = get_color_info(probes);
    size_t count = 0;
    for (const Word *w = words.arr; w < words.arr + words.size; w++) {
        if (word_matches(*w, &color_info)) {
            count++;
        }
    }
    return count;
}

static uint64_t get_possible_count(WordArray words, size_t guess_index, Word actual) {
    Word guess = WORDS[guess_index];
    Word result = generate_result(guess, actual);

    uint64_t cached = get_cached_count(guess_index, result);
    if (CACHE_MISS != cached) {
        return cached;
    }

    Probe probe = { .guess = guess, .result = result };
    uint64_t count = count_filtered_words(words, (ProbeArray) { .arr = &probe, .size = 1 });
    put_cached_count(guess_index, result, count);
    return count;
}

static int compare_word_amount(const void* a, const void* b) {
    const WordAmount *wa = a;
    const WordAmount *wb = b;
    return wa->amount - wb->amount;
}


#define THREADS_COUNT 16

typedef struct {
    size_t guess_from;
    size_t guess_to;
    WordArray possible_actuals;
    WordAmount* guesses;
} ThreadInfo;

static void* thread_routine(void* arg) {
    ThreadInfo* info = arg;
    const Word* possible_actuals = info->possible_actuals.arr;
    size_t pa_count = info->possible_actuals.size;
    for (size_t guess_i = info->guess_from; guess_i < info->guess_to; guess_i++) {
        size_t possible_count = 0;
        for (const Word* pa = possible_actuals; pa < possible_actuals + pa_count; pa++) {
            WordArray arr = { .arr = possible_actuals, .size = pa_count };
            possible_count += get_possible_count(arr, guess_i, *pa);
        }
        info->guesses[guess_i] = (WordAmount) { .word = WORDS[guess_i], .amount = possible_count };
    }
    return NULL;
}

Word guess_word(void) {
    if (get_probes_count() == 0) {
        printf("Possible words: %zu\n", WORDS_COUNT);
        return Word_from_str("lares");
    }

    WordAmount guesses[WORDS_COUNT] = {0};

    Word possible_actuals[WORDS_COUNT] = {0};
    const size_t pa_count  = filter_words(
            possible_actuals,
            (WordArray)  { .arr = WORDS,  .size = WORDS_COUNT },
            (ProbeArray) { .arr = PROBES, .size = PROBES_COUNT });
    printf("Possible words: %zu\n", pa_count);

    if (pa_count < 100) {
        for (size_t i = 0; i < pa_count; i++) {
            printf("%.*s ", WORD_LEN, possible_actuals[i].val);
        }
        puts("");
    }

    clear_cache();

    pthread_t threads[THREADS_COUNT] = {0};
    ThreadInfo info_arr[THREADS_COUNT] = {0};
    for (int i = 0; i < THREADS_COUNT; i++) {
        int last_additional = (i == THREADS_COUNT - 1) ? (WORDS_COUNT % THREADS_COUNT) : 0;
        size_t guess_from = WORDS_COUNT / THREADS_COUNT * i;
        size_t guess_to = guess_from + WORDS_COUNT / THREADS_COUNT + last_additional;
        info_arr[i] = (ThreadInfo) {
            .guess_from = guess_from,
            .guess_to = guess_to,
            .possible_actuals = (WordArray) { .arr = possible_actuals, .size = pa_count },
            .guesses = guesses,
        };
        if (pthread_create(threads + i, NULL, thread_routine, info_arr + i) != 0) {
            fprintf(stderr, "cannot create thread\n");
            exit(-1);
        }
    }
    for (int i = 0; i < THREADS_COUNT; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            fprintf(stderr, "cannot join thread\n");
            exit(-1);
        }
    }

    qsort(guesses, WORDS_COUNT, sizeof(guesses[0]), compare_word_amount);

    for (size_t guess_i = 0; guess_i < WORDS_COUNT; guess_i++) {
        if (guesses[guess_i].amount > guesses[0].amount) break;
        for (size_t pa_i = 0; pa_i < pa_count; pa_i++) {
            if (Word_equals(guesses[guess_i].word, possible_actuals[pa_i])) {
                return guesses[guess_i].word;
            }
        }
    }
    return guesses[0].word;
}

// cache stuff
static int get_cached_count_index(Word result) {
    int index = 0;
    for (int i = 0; i < WORD_LEN; i++) {
        int value;
        switch (result.val[i]) {
            case GRAY:   value = 0; break;
            case YELLOW: value = 1; break;
            case GREEN:  value = 2; break;
            default:
                fprintf(stderr, "unknown color %d", result.val[i]);
                exit(-1);
        }
        index *= 3;
        index += value;
    }
    return index;
}

static uint64_t get_cached_count(size_t guess_index, Word result) {
    return count_cache[guess_index][get_cached_count_index(result)];
}

static void put_cached_count(size_t guess_index, Word result, uint64_t count) {
    count_cache[guess_index][get_cached_count_index(result)] = count;
}

static void clear_cache(void) {
    for (size_t i = 0; i < sizeof(count_cache) / sizeof(count_cache[0]); i++) {
        for (size_t j = 0; j < sizeof(count_cache[0]) / sizeof(count_cache[0][0]); j++) {
            count_cache[i][j] = CACHE_MISS;
        }
    }
}
// cache stuff END

// not interesting bullshit
bool is_result_valid(Word result) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (result.val[i] != GRAY && result.val[i] != YELLOW && result.val[i] != GREEN) {
            return false;
        }
    }
    return true;
}


void save_probe(Probe probe) {
    PROBES[PROBES_COUNT++] = probe;
}

int get_probes_count(void) {
    return PROBES_COUNT;
}


bool Word_equals(Word a, Word b) {
    for (int i = 0; i < WORD_LEN; i++) {
        if (a.val[i] != b.val[i]) return false;
    }
    return true;
}

Word Word_from_str(const char* str) {
    return *(Word*)str;
}


static bool CharSet_contains(CharSet set, char ch) {
    return (set & (1 << (ch - 'a'))) != 0;
}

static bool CharSet_is_empty(CharSet set) {
    return set == 0;
}

static void CharSet_add(CharSet *set, char ch) {
    *set |= (1 << (ch - 'a'));
}

static void CharSet_remove(CharSet *set, char ch) {
    *set &= ~(1 << (ch - 'a'));
}
