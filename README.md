# cwordle - wordle clone

Wordle clone written in C.

## Building

Run `make cwordle` to build the program. `make` variables are:

- `CC`: the C compiler (`gcc`)
- `CFLAGS`: the C compiler flags (`-Wall`)

## Command line

Call `cwordle WORD-LIST` to play the game. The word list must be a plaintext
list of five-character words alphacetically sorted sorted (use `cat` and `sort`)
to add words to the list in the right order.

## Playing

The play field does not take control of the whole terminal screen, instead just
the area down of the cursor. The game plays like any other wordle clone: black
background for answer-missing letters, yellow for letters in other position,
green for letters on the right position.

The matching algorithm is not greedy, meaning the answer "spool" with the guess
"ooops" will match like: BLACK, YELLOW, GREEN, BLACK, YELLOW.

## Input

Press ENTER to submit a word, it'll do nothing if you have less than five
characters. If the word is not in the word list, it'll output:

- NOT <WORD>

in the output field.

Press BACKSPACE to delete a character.

## Output

The game may end with either of these two messages:

- OK <WORD> <TRIES>/6
- FAIL <WORD>

You can parse this output to make a statistics file of your own.

## License

This repository is licensed under the [MIT
License](https://opensource.org/licenses/MIT).
