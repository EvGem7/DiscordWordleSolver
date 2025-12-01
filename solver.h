#ifndef SOLVER_H_
#define SOLVER_H_

#include <stdint.h>

#define WORD_LEN 5
#define MAX_WORDS 100000
#define MAX_PROBES 128
#define GREEN '3'
#define YELLOW '2'
#define GRAY '1'

typedef struct {
    char val[WORD_LEN];
} Word;

typedef struct {
    Word guess;
    Word result;
} Probe;

extern Word words[MAX_WORDS];
extern uint32_t words_count;

extern Probe probes[MAX_PROBES];
extern uint32_t probes_count;


bool is_word_valid(Word word);
bool is_result_valid(const char *result);

Word guess_word(void);

#endif//SOLVER_H_ 

