#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "solver.h"
#include "words.c"

#ifdef DEBUG
#define LOG_DEBUG(...) solver_printf(__VA_ARGS__)
#else
#define LOG_DEBUG(...)
#endif

static Probe PROBES[MAX_PROBES] = {0};
static uint32_t PROBES_COUNT = 0;


#define RESULT_MAP_SIZE 243 // 243 = 3 ** 5; 3 stands for 3 possible result colors
typedef union {
    uint64_t u64;
    double f64;
} ResultMapValue;

typedef struct {
    ResultMapValue values[RESULT_MAP_SIZE]; 
} ResultMap;

static int              ResultMap_get_index(Word result);


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
    double entropy;
} WordEntropy;


Word generate_result(Word guess, Word actual) {
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

static size_t filter_words(Word *restrict dst, WordArray words, ProbeArray probes) {
    size_t count = 0;
    for (const Word *w = words.arr; w < words.arr + words.size; w++) {
        bool matches = true;
        for (const Probe *probe = probes.arr; probe < probes.arr + probes.size; probe++) {
            Word result = generate_result(probe->guess, *w);
            if (!Word_equals(probe->result, result)) {
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

static int compare_word_entropy(const void* a, const void* b) {
    const WordEntropy *wa = a;
    const WordEntropy *wb = b;
    return ((wa->entropy < wb->entropy) ? 1 : 0) - ((wa->entropy > wb->entropy) ? 1 : 0);
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

static WordEntropy      GUESSES[WORDS_COUNT];
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
            ResultMap map = {0};
            Word guess = WORDS[guess_i];
            for (const Word* pa = POSSIBLE_ACTUALS; pa < POSSIBLE_ACTUALS + PA_COUNT; pa++) {
                Word result = generate_result(guess, *pa);
                map.values[ResultMap_get_index(result)].u64++;
            }
            double entropy = 0.0;
            for (int i = 0; i < RESULT_MAP_SIZE; i++) {
                uint64_t count = map.values[i].u64;
                if (count <= 0) continue;
                double p = (double) count / PA_COUNT;
                entropy += -p * log2(p);
            }
            GUESSES[guess_i] = (WordEntropy) { .word = WORDS[guess_i], .entropy = entropy };
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
//    if (get_probes_count() == 0) {
//        solver_printf("Possible words: %zu\n", WORDS_COUNT);
//        return Word_from_str("tares"); // precomputed
//    }

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

    init_workers();
    lock_mutex();
    LOG_DEBUG("guess_word() sending WORK_AVAILABLE; READY_WORKERS = %d\n", READY_WORKERS);
    signal_cond(&WORK_AVAILABLE);
    READY_WORKERS = 0;
    unlock_mutex();
    LOG_DEBUG("guess_word() waiting for workers idle; READY_WORKERS = %d\n", READY_WORKERS);
    wait_workers_idle();
    LOG_DEBUG("guess_word() starting sorting; READY_WORKERS = %d\n", READY_WORKERS);

    qsort(GUESSES, WORDS_COUNT, sizeof(GUESSES[0]), compare_word_entropy);

    for (size_t guess_i = 0; guess_i < WORDS_COUNT; guess_i++) {
        if (GUESSES[guess_i].entropy > GUESSES[0].entropy) break;
        for (size_t pa_i = 0; pa_i < PA_COUNT; pa_i++) {
            if (Word_equals(GUESSES[guess_i].word, POSSIBLE_ACTUALS[pa_i])) {
                return GUESSES[guess_i].word;
            }
        }
    }
    return GUESSES[0].word;
}

// cache stuff
static int ResultMap_get_index(Word result) {
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


int (*solver_printf)(const char *restrict format, ...) = printf;

