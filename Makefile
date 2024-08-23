default: cwordle

CC?=gcc
CFLAGS?=-Wall

OBJECTS:=words.o cwordle.o
TARGETS:=cwordle

cwordle: cwordle.o words.o
	@echo "CC -o " cwordle
	@$(CC) $(CFLAGS) cwordle.o words.o -o cwordle

words.c: words.h

cwordle.o: cwordle.c words.c
	@echo "CC -o" cwordle.o
	@$(CC) $(CFLAGS) -c cwordle.c -o cwordle.o

words.o: words.c
	@echo "CC -o" words.o
	@$(CC) $(CFLAGS) -c words.c -o words.o

clean:
	@echo "RM" $(OBJECTS) $(TARGETS)
	@rm -rfv $(OBJECTS) $(TARGETS)

.PHONY: clean
