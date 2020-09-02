CC = gcc
CFLAGS = -Wall -pedantic -march=native -O2
LDFLAGS =
SOURCES = fbclock.c
OBJECTS = $(SOURCES:.c=.o)
EXE = fbclock

$(EXE): $(OBJECTS)
	$(CC) $^ $(LDFLAGS) -o $(EXE)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY : clean
clean:
	rm -f *.o $(EXE) core

