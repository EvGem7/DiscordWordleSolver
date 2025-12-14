#include <stdio.h>

#include "solver.h"

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("provide secret and guess\n");
        return -1;
    }
    Word actual = Word_from_str(argv[1]);
    Word guess =  Word_from_str(argv[2]);
    Word result = generate_result(guess, actual);
    printf("%.5s\n", result.val);
    return 0;
}
