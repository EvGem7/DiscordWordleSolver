#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <stdbool.h>

#include "solver.h"
#include "words.c"

#ifdef DEBUG
#define LOG_DEBUG(...) solver_printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

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
            // TODO: check if guess.val[yc_i] != yellow_check.val[yc_i] needs to be replaced with guess.val[yc_i] != actual.val[yc_i]
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



// THREADING STUFF
#define WORKERS_COUNT 16

typedef struct {
    size_t guess_from;
    size_t guess_to;
} WorkerInfo;

static pthread_mutex_t  MUTEX                   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   WORK_AVAILABLE          = PTHREAD_COND_INITIALIZER;
static pthread_cond_t   WORKER_READY            = PTHREAD_COND_INITIALIZER;
static uint8_t          READY_WORKERS           = 0;
static bool             ARE_WORKERS_INITIALIZED = false;
static pthread_t        WORKERS[WORKERS_COUNT];
static WorkerInfo       WORKERS_INFO[WORKERS_COUNT];

static WordAmount       GUESSES[WORDS_COUNT];
static Word             POSSIBLE_ACTUALS[WORDS_COUNT];
static size_t           PA_COUNT = 0;

static void lock_mutex() {
    if (pthread_mutex_lock(&MUTEX) != 0) {
        fprintf(stderr, "cannot lock mutex\n");
        exit(-1);
    }
}

static void unlock_mutex() {
    if (pthread_mutex_unlock(&MUTEX) != 0) {
        fprintf(stderr, "cannot unlock mutex\n");
        exit(-1);
    }
}

static void wait_for_cond(pthread_cond_t* cond) {
    if (pthread_cond_wait(cond, &MUTEX) != 0) {
        fprintf(stderr, "failed waiting for condition\n");
        exit(-1);
    }
}

static void signal_cond(pthread_cond_t* cond) {
    if (pthread_cond_broadcast(cond) != 0) {
        fprintf(stderr, "failed signal condition\n");
        exit(-1);
    }
}

static void* worker_routine(void* arg) {
    WorkerInfo* info = arg;
    while (true) {
        lock_mutex();
        READY_WORKERS++;
        LOG_DEBUG("worker_routine(#%05zu) sending WORKER_READY READY_WORKERS = %d\n", info->guess_from, READY_WORKERS);
        signal_cond(&WORKER_READY);
        LOG_DEBUG("worker_routine(#%05zu) waiting for WORK_AVAILABLE\n", info->guess_from);
        wait_for_cond(&WORK_AVAILABLE);
        LOG_DEBUG("worker_routine(#%05zu) starting work READY_WORKERS = %d\n", info->guess_from, READY_WORKERS);
        unlock_mutex();

        for (size_t guess_i = info->guess_from; guess_i < info->guess_to; guess_i++) {
            size_t possible_count = 0;
            for (const Word* pa = POSSIBLE_ACTUALS; pa < POSSIBLE_ACTUALS + PA_COUNT; pa++) {
                WordArray arr = { .arr = POSSIBLE_ACTUALS, .size = PA_COUNT };
                possible_count += get_possible_count(arr, guess_i, *pa);
            }
            GUESSES[guess_i] = (WordAmount) { .word = WORDS[guess_i], .amount = possible_count };
        }
    }
    return NULL;
}

static void wait_workers_idle(void) {
    while (true) {
        lock_mutex();
        LOG_DEBUG("wait_workers_idle() check condition; READY_WORKERS = %d\n", READY_WORKERS);
        if (READY_WORKERS >= WORKERS_COUNT) {
            unlock_mutex();
            break;
        }
        LOG_DEBUG("wait_workers_idle() waiting for WORKER_READY; READY_WORKERS = %d\n", READY_WORKERS);
        wait_for_cond(&WORKER_READY);
        LOG_DEBUG("wait_workers_idle() received for WORKER_READY; READY_WORKERS = %d\n", READY_WORKERS);
        unlock_mutex();
    }
}

static void init_workers(void) {
    if (ARE_WORKERS_INITIALIZED) return;
    for (int i = 0; i < WORKERS_COUNT; i++) {
        int last_additional = (i == WORKERS_COUNT - 1) ? (WORDS_COUNT % WORKERS_COUNT) : 0;
        size_t guess_from = WORDS_COUNT / WORKERS_COUNT * i;
        size_t guess_to = guess_from + WORDS_COUNT / WORKERS_COUNT + last_additional;
        WORKERS_INFO[i] = (WorkerInfo) {
            .guess_from = guess_from,
            .guess_to = guess_to,
        };
        if (pthread_create(WORKERS + i, NULL, worker_routine, WORKERS_INFO + i) != 0) {
            fprintf(stderr, "cannot create worker thread\n");
            exit(-1);
        }
    }
    wait_workers_idle();
    ARE_WORKERS_INITIALIZED = true;
}
// THREADING STUFF END



Word guess_word(void) {
    if (get_probes_count() == 0) {
        solver_printf("Possible words: %zu\n", WORDS_COUNT);
        return Word_from_str("lares"); // precomputed
    }

    PA_COUNT = filter_words(
            POSSIBLE_ACTUALS,
            (WordArray)  { .arr = WORDS,  .size = WORDS_COUNT },
            (ProbeArray) { .arr = PROBES, .size = PROBES_COUNT });
    solver_printf("Possible words: %zu\n", PA_COUNT);

    if (PA_COUNT < 100) {
        for (size_t i = 0; i < PA_COUNT; i++) {
            solver_printf("%.*s ", WORD_LEN, POSSIBLE_ACTUALS[i].val);
        }
        solver_printf("\n");
    }

    clear_cache();

    init_workers();
    lock_mutex();
    LOG_DEBUG("guess_word() sending WORK_AVAILABLE; READY_WORKERS = %d\n", READY_WORKERS);
    signal_cond(&WORK_AVAILABLE);
    READY_WORKERS = 0;
    unlock_mutex();
    LOG_DEBUG("guess_word() waiting for workers idle; READY_WORKERS = %d\n", READY_WORKERS);
    wait_workers_idle();
    LOG_DEBUG("guess_word() starting sorting; READY_WORKERS = %d\n", READY_WORKERS);

    qsort(GUESSES, WORDS_COUNT, sizeof(GUESSES[0]), compare_word_amount);

    for (size_t guess_i = 0; guess_i < WORDS_COUNT; guess_i++) {
        if (GUESSES[guess_i].amount > GUESSES[0].amount) break;
        for (size_t pa_i = 0; pa_i < PA_COUNT; pa_i++) {
            if (Word_equals(GUESSES[guess_i].word, POSSIBLE_ACTUALS[pa_i])) {
                return GUESSES[guess_i].word;
            }
        }
    }
    return GUESSES[0].word;
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

void reset_probes(void) {
    PROBES_COUNT = 0;
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

int (*solver_printf)(const char *restrict format, ...) = printf;

