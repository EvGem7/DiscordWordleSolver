all: solver solver_entropy test test_entropy generate_result_test

_FLAGS := -Wall -Wextra -O3

solver: main.c solver.c solver.h words.c
	cc $(_FLAGS) $(FLAGS) main.c solver.c -o solver

solver_entropy: main.c solver_entropy.c solver.h words.c
	cc $(_FLAGS) $(FLAGS) main.c solver_entropy.c -o solver_entropy -lm

test: test.c solver.c solver.h words.c
	cc $(_FLAGS) $(FLAGS) test.c solver.c -o test

test_entropy: test.c solver_entropy.c solver.h words.c
	cc $(_FLAGS) $(FLAGS) test.c solver_entropy.c -o test_entropy -lm

generate_result_test: solver.h solver.c generate_result_test.c
	cc $(_FLAGS) $(FLAGS) generate_result_test.c solver.c -o generate_result_test

clean:
	rm -fv solver solver_entropy test test_entropy generate_result_test

.PHONY: clean all
