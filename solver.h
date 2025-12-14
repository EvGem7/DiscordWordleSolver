#ifndef SOLVER_H_
#define SOLVER_H_

#include <stdint.h>

#define WORD_LEN 5
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

bool Word_equals(Word a, Word b);
Word Word_from_str(const char* str);

bool is_result_valid(Word result);

void save_probe(Probe probe);
int get_probes_count(void);
void reset_probes(void);

Word generate_result(Word guess, Word actual);

Word guess_word(void);

extern int (*solver_printf)(const char *restrict format, ...);

#endif //SOLVER_H_ 

