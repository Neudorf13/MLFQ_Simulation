CC = clang
CFLAGS = -Wall -Wextra -Werror -g

SRCS = Simulation_Main.c Queue_Support.c
OBJS = $(SRCS:.c=.o)
EXEC = simulation

$(EXEC): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(OBJS) $(EXEC)

