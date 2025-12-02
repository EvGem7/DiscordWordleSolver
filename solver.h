#ifndef SOLVER_H_
#define SOLVER_H_

#include <stdint.h>

#define WORD_LEN 5
#define MAX_WORDS 20000
#define MAX_PROBES 128

typedef char Color;
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

bool is_word_valid(Word word);
bool is_result_valid(Word result);

void save_word(Word word);
void save_probe(Probe probe);

int get_words_count(void);
int get_probes_count(void);

Word guess_word(void);

#endif//SOLVER_H_ 

