_FLAGS := -Wall -Wextra -O3

solver: main.c solver.c solver.h Makefile words.c
	gcc $(_FLAGS) $(FLAGS) main.c solver.c -o solver
