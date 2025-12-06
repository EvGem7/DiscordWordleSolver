_FLAGS := -Wall -Wextra -O3

solver: main.c solver.c solver.h words.c
	cc $(_FLAGS) $(FLAGS) main.c solver.c -o solver

test: test.c solver.c solver.h words.c
	cc $(_FLAGS) $(FLAGS) test.c -o test
