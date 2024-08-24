#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <fcntl.h>
#include <error.h>

// ASCII normalization of `a'-`z' range (97-122 -> 0-25)
#define ASCII_NORM(x) ((x) - 97)

#define S(x) (x), (sizeof(x)/sizeof(*x) - 1)
#define LN(x) S((x "\n"))
#define MAX(x, y) ((x) > (y)? (x): (y))

typedef unsigned int uint;
typedef unsigned char uchar;
typedef char bool;

#define true 1
#define false 0

static inline char at2(uchar* dat, uint at) {
  char _at = (at & 0x03) << 1;
  char mask = (1 << _at) | (1 << (_at + 1));

  return (dat[at >> 2] & mask) >> _at;
}

// `new' is two bit data
static inline void at2_set(uchar* dat, uint at, char new) {
  char _at = (at & 0x03) << 1;
  new <<= _at;

  dat[at >> 2] |= new;
}

#define COL_GREEN_BG "102"
#define COL_YELLOW_BG "103"
#define COL_BLACK_BG "100"
#define COL_BLACK_FG "90"
#define COL_NONE "0"

//// WORDLE UTILS

typedef struct {
  char* buf;
  char* draw_head;

  uint size;
} wordle_display;

typedef enum {
  WORDLE_CHAR_NOT,
  WORDLE_CHAR_OTHER,
  WORDLE_CHAR_IN,
} wordle_chk;

static char buf[1<<9];

static inline wordle_display
wordle_draw(char* str, uint size, wordle_display dpy) {
  char* buf = dpy.draw_head;

  for (uint i = 0; i < size; i++) {
    buf[i] = str[i];
  }

  dpy.draw_head = buf + size;

  // update the `size' field only if draw_head exceeded the maximum
  if (dpy.draw_head >= dpy.buf + size) {
    dpy.size += size;
  }

  return dpy;
}

static inline void wordle_display_flush(wordle_display* dpy) {
  write(STDOUT_FILENO, dpy->buf, dpy->size);

  dpy->draw_head = dpy->buf;
  dpy->size = 0;
}

static inline void wordle_display_flush_str(char* str, uint size) {
  write(STDOUT_FILENO, str, size);
}

typedef struct {
  uint k_pos;
  wordle_chk chk;
} wordle_key_map_t;

static const char* wordle_kbd_qwerty[] = {
  "qwertyuiop",
   "asdfghjkl",
    "zxcvbnm"
};

typedef enum {
  CUR_UP = 'A',
  CUR_DOWN = 'B',
  CUR_RIGHT = 'C',
  CUR_LEFT = 'D',
} cur_move_t;

static struct termios term_old;
static struct termios term_new;

static inline void wordle_term_init(void) {
  tcgetattr(STDIN_FILENO, &term_old);
  term_new = term_old;

  term_new.c_lflag &= ~(ICANON|ECHO);

  tcsetattr(STDIN_FILENO, TCSANOW, &term_new);
}

static inline void wordle_term_exit(void) {
  tcsetattr(STDIN_FILENO, TCSANOW, &term_old);
}

static inline wordle_display
wordle_cur_move(cur_move_t t, uint n, wordle_display dpy) {
  uint i = 2;
  char cmd[6] = "\x1b[";

  // up to three digits are supported
  uint mod = 10;

  for (uint _ = 0; _ < 3; _++) {
    uint dig = (n % mod) + 48; // ASCII encode
    cmd[i] = dig;
    i++;

    if (!(n = (n / mod))) {
      break;
    }

    mod *= 10;
  }

  cmd[i] = (char) t;
  i++;

  return wordle_draw(cmd, i, dpy);
}

//// GAME

static inline char* wordle_answer_get(int wordsfd) {
  // DEBUG
  return "plane";
}

static char* wordle_word_get(char* input) {
  char str[1];
  char fixbuf[32] = {0};

  uint letters = 0;

loop:
  for (char c;;)  {
    c = getchar();

    switch (c) {
    case 0x7f:
      // put the cursor left (unless it's at the edge), and output a dot where
      // we were
      if (letters) {
        wordle_display fixdpy = {.draw_head = fixbuf, .buf = fixbuf};

        fixdpy = wordle_cur_move(CUR_LEFT, 1, fixdpy);
        fixdpy = wordle_draw(".", 1, fixdpy);
        fixdpy = wordle_cur_move(CUR_LEFT, 1, fixdpy);

        wordle_display_flush(&fixdpy);

        letters--;
      }

      break;
    case 0x0a:
      if (letters == 5) {
        goto done;
      }
    }

    if (c >= 'a' && c <= 'z' && letters < 5) {
      // echo the letter out
      str[0] = c;
      wordle_display_flush_str(str, 1);

      input[letters] = c;

      letters++;
    }
  }

done:
  // TODO: check the word against the word list
  return input;
}

typedef struct {
  char c;
  wordle_chk t;
} wordle_chk_word[5];

// typedef struct {
//   char c;
//   wordle_chk t;
// } wordle_chk_dpy_t[5];

typedef uchar wordle_chk_dpy[(26*2) / (sizeof(char)*8) + 1];

static inline wordle_chk
wordle_char_check(char c, uint pos, char* ans, uchar* nn) {
  // how many repeting `c' we have on the answer. useful for having `c' be both
  // `WORDLE_CHAR_IN' and `WORDLE_CHAR_OTHER', for example:
  //      answer: moons, input: spool (3rd `o' is IN, 4th `o' is OTHER )
  uint rep = 0;
  uint n = ASCII_NORM(c);

  wordle_chk chk = WORDLE_CHAR_NOT;

  // `c' is a character of `ans' and in the right position: `WORDLE_CHAR_IN'
  if (ans[pos] == c) {
    nn[n]++;
    return WORDLE_CHAR_IN;
  }

  for (uint i = 0; i < 5; i++) {
    if (ans[i] == c) {
      rep++;
    }
  }

  if ((nn[n] + 1) <= rep) {
    nn[n]++;
    return WORDLE_CHAR_OTHER;
  }

  return chk;
}

static wordle_display
wordle_draw_color(char* str, uint size, bool contrast, wordle_display dpy) {
  dpy = wordle_draw(S("\x1b["), dpy);

  dpy = wordle_draw(str, size, dpy);

  if (contrast) {
    dpy = wordle_draw(S(";30"), dpy);
  }

  dpy = wordle_draw(S("m"), dpy);

  return dpy;
}

static wordle_display
wordle_display_chk(wordle_chk_word word_chk, wordle_display dpy) {
  char cbuf[1];

  enum {
    NONE,
    GREEN,
    YELLOW,
    BLACK,
  } lc = NONE;

  dpy = wordle_cur_move(CUR_LEFT, 5, dpy);

  for (uint i = 0; i < 5; i++) {
    switch (word_chk[i].t) {
    case WORDLE_CHAR_IN:
      if (lc != GREEN) {
        dpy = wordle_draw_color(S(COL_GREEN_BG), !lc, dpy);
      }

      lc = GREEN;
      goto draw_c;

    case WORDLE_CHAR_OTHER:
      if (lc != YELLOW) {
        dpy = wordle_draw_color(S(COL_YELLOW_BG), !lc, dpy);
      }

      lc = YELLOW;
      goto draw_c;

    case WORDLE_CHAR_NOT: ;
      // hit without any colors: reset `lc'
      if (lc) {
        lc = NONE;
        dpy = wordle_draw_color(S(COL_NONE), false, dpy);
      }

draw_c:
      cbuf[0] = word_chk[i].c;

      dpy = wordle_draw(cbuf, 1, dpy);
      break;
    }
  }

  dpy = wordle_draw_color(S(COL_NONE), false, dpy);

  wordle_display_flush(&dpy);

  return dpy;
}

static wordle_display
wordle_display_kbd_row(wordle_chk_dpy chk_dpy, uint key_n, uint row,
                       wordle_display dpy) {
  bool lc = false;
  char buf[1];

  for (uint key = 0; key < key_n; key++) {

    buf[0] = wordle_kbd_qwerty[row][key];
    wordle_chk chk = at2(chk_dpy, ASCII_NORM(buf[0]));

    if (chk) {
      chk--;

      switch (chk) {
      case WORDLE_CHAR_IN:
        dpy = wordle_draw_color(S(COL_GREEN_BG), true, dpy);
        lc = true;
        break;
      case WORDLE_CHAR_NOT:
        dpy = wordle_draw_color(S(COL_BLACK_BG), true, dpy);
        lc = true;
        break;
      case WORDLE_CHAR_OTHER:
        dpy = wordle_draw_color(S(COL_YELLOW_BG), true, dpy);
        lc = true;
        break;
      }
    }

    dpy = wordle_draw(buf, 1, dpy);

    if (lc) {
      lc = false;
      dpy = wordle_draw_color(S(COL_NONE), false, dpy);
    }

    if (key != (key_n - 1)) {
      dpy = wordle_draw(S(" "), dpy);
    }
  }

  dpy = wordle_draw(S("\n"), dpy);
  wordle_display_flush(&dpy);

  return dpy;
}

static wordle_display
wordle_display_kbd(wordle_chk_dpy chk_dpy, uint line, wordle_display dpy) {
  // move the cursor left of the input fields, and down to the keyboard `^'
  dpy = wordle_cur_move(CUR_LEFT, 16, dpy);
  dpy = wordle_cur_move(CUR_DOWN, 7 - line, dpy);

  wordle_display_flush(&dpy);

  // update the keyboard display
  for (uint row = 0; row < 3; row++) {
    uint key_n = 0;

    dpy = wordle_draw(S("     "), dpy);

    switch (row) {
    case 0:
      key_n = 10;
      break;
    case 1:
      dpy = wordle_draw(S(" "), dpy);

      key_n = 9;
      break;
    case 2:
      dpy = wordle_draw(S("   "), dpy);

      key_n = 7;
      break;
    }

    dpy = wordle_display_kbd_row(chk_dpy, key_n, row, dpy);
  }

  dpy = wordle_cur_move(CUR_UP, 9 - line, dpy);
  dpy = wordle_cur_move(CUR_RIGHT, 11, dpy);

  wordle_display_flush(&dpy);

  return dpy;
}

static bool
wordle_word_check(char* input, char* ans, uint line, wordle_chk_dpy* chk_dpy) {
  wordle_chk_word chk = {0};

  bool win = true;

  // how many times `c' is non-nil with respect to matches
  uchar nonnil[26] = {0};

  for (uint i = 0; i < 5; i++) {
    char c = input[i];
    chk[i].c = c;

    wordle_chk wc = wordle_char_check(chk[i].c, i, ans, nonnil);
    chk[i].t = wc;

    win &= (chk[i].t == WORDLE_CHAR_IN);

    wordle_chk dc = at2(*chk_dpy, ASCII_NORM(c));

    // we use `1 + wordle_chk', so that we can differentiate between _NOT and
    // some key that was never pressed
    if (!dc || MAX(dc - 1, wc) == wc) {
      at2_set(*chk_dpy, ASCII_NORM(c), wc + 1);
    }
  }

  wordle_display dpy = {.buf = buf, .draw_head = buf, .size = 0};

  dpy = wordle_display_chk(chk, dpy);

  wordle_chk_dpy chkdpy;
  memcpy(chkdpy, chk_dpy, 7);

  dpy = wordle_display_kbd(chkdpy, line, dpy);

  return win;
}

static void wordle_game(char* ans) {
  char input[5] = {0};

  wordle_chk_dpy chk_dpy = {0};
  wordle_display edpy = {.buf = buf, .draw_head = buf, .size = 0};

  bool won = false;

  for (uint try = 0; try < 6; try++) {
    wordle_word_get(input);

    won = wordle_word_check(input, ans, try, &chk_dpy);

    if (won) {
      edpy = wordle_cur_move(CUR_DOWN, 8 - try, edpy);
      edpy = wordle_draw(LN("\n"), edpy);

      wordle_display_flush(&edpy);

      printf("OK %s %d/6\n", ans, try+1);

      return;
    }

    if (try == 5) {
      edpy = wordle_cur_move(CUR_DOWN, 8 - try, edpy);
      edpy = wordle_draw(LN("\n"), edpy);

      wordle_display_flush(&edpy);

      printf("FAIL %s\n", ans);

      return;
    }
  }
}

//// HELPERS

static inline int help(void) {
  puts("Usage:       cwordle WORD-LIST");
  puts(\
"Description: Wordle clone written in C. Will init a game of wordle given the\n"
"             WORD-LIST. It must be a list of 5-char words separated by\n"
"             newlines");

  return 0;
}

//// DISPLAY

static wordle_display wordle_display_setup(wordle_display dpy) {
  dpy.size = 0;
  dpy.buf = dpy.draw_head = buf;

  // first two lines are simply dots
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);

  // 3rd to 5th lines have the keyboard after. the keyboard has a 5 char offset
  // from the word input
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);

  // the last line is simply the dots again
  dpy = wordle_draw(LN("           .....\n"), dpy);

  dpy = wordle_draw(LN("     q w e r t y u i o p"), dpy);
  dpy = wordle_draw(LN("      a s d f g h j k l"), dpy);
  dpy = wordle_draw(LN("        z x c v b n m\n"), dpy);

  // dpy = wordle_draw(S("\x1b[?25l"), dpy);

  dpy = wordle_cur_move(CUR_UP, 11, dpy);
  dpy = wordle_cur_move(CUR_RIGHT, 11, dpy);

  wordle_display_flush(&dpy);

  return dpy;
}

//// MAIN GAME

static int wordle_main(int wordsfd) {
  if (wordsfd <= 0) {
    puts("[ !! ] Missing word list. See `--help'.");
    return 1;
  }

  char* ans = wordle_answer_get(wordsfd);
  wordle_display dpy = {.buf = buf};

  wordle_term_init();

  dpy = wordle_display_setup(dpy);

  wordle_game(ans);

  wordle_term_exit();

  return 0;
}

////

int main(int argc, char** argv) {
  int wordsfd = 0;

  for (uint i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      help();
    }
    else {
      wordsfd = open(argv[i], 0);

      if (wordsfd <= 0) {
        puts("[ !! ] System error.");
        return 1;
      }
    }
  }

  return wordle_main(wordsfd);
}
