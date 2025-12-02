FLAGS := -Wall -Wextra -O3

solver: main.c solver.c solver.h Makefile
	gcc $(FLAGS) main.c solver.c -o solver
