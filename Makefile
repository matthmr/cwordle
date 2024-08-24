default: cwordle

CC?=gcc
CFLAGS?=-Wall

OBJECTS:=cwordle.o
TARGETS:=cwordle

cwordle: cwordle.o
	@echo "CC -o " cwordle
	@$(CC) $(CFLAGS) cwordle.o words.o -o cwordle

cwordle.o: cwordle.c
	@echo "CC -o" cwordle.o
	@$(CC) $(CFLAGS) -c cwordle.c -o cwordle.o

clean:
	@echo "RM" $(OBJECTS) $(TARGETS)
	@rm -rfv $(OBJECTS) $(TARGETS)

.PHONY: clean
