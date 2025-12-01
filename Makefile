FLAGS := -Wall -Wextra

solver: main.c solver.c solver.h
	gcc $(FLAGS) main.c solver.c -o solver
