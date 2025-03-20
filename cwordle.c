#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>

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

// three-bit bitfield get
static inline char at3(uchar* dat, uint at) {
  uchar idx = at % 2;
  uchar mask = 0x7 << (3*idx);

  return (dat[at/2] & mask) >> (3*idx);
}

// three-bit bitfield set
static inline void at3_set(uchar* dat, uint at, uchar new) {
  uchar idx = at % 2;

  new = (new & 0x7) << (3*idx);

  // zero out the current bits, then or the new result
  dat[at/2] = (dat[at/2] & ~(0x7 << (3*idx))) | new;
}

// two-bit bitfield get
static inline char at2(uchar* dat, uint at) {
  uchar idx = at % 4;
  uchar mask = 0x3 << (2*idx);

  return (dat[at/4] & mask) >> (2*idx);
}

// two-bit bitfield set
static inline void at2_set(uchar* dat, uint at, uchar new) {
  uchar idx = at % 4;

  new = (new & 0x3) << (2*idx);

  // zero out the current bits, then or the new result
  dat[at/4] = (dat[at/4] & ~(0x3 << (2*idx))) | new;
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

static char buf[1<<9], iobuf[4096];

typedef char wordle_word[5];

typedef struct {
  wordle_word* list;
  uint size;
} wordle_words;

static wordle_words word_list;

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

  CUR_DOWN_ABS = 'E',
  CUR_UP_ABS = 'F',
} cur_move_t;

static struct termios term_old;
static struct termios term_new;

static void term_exit(int _sig) {
  tcsetattr(STDIN_FILENO, TCSANOW, &term_old);
  puts("\e[?25h");

  if (_sig) {
    raise(SIGTERM);
  }
}

static inline void wordle_term_exit(void) {
  term_exit(0);
}

static inline void wordle_term_init(void) {
  tcgetattr(STDIN_FILENO, &term_old);
  term_new = term_old;

  term_new.c_lflag &= ~(ICANON|ECHO);

  tcsetattr(STDIN_FILENO, TCSANOW, &term_new);
  puts("\e[?25l");

  signal(SIGINT, term_exit);
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

  // reverse the result
  for (uint j = 2; j <= i/2; j++) {
    char o = cmd[j];
    uint oi = (i - 1) - (j - 2);

    cmd[j] = cmd[oi];
    cmd[oi] = o;
  }

  cmd[i] = (char) t;
  i++;

  return wordle_draw(cmd, i, dpy);
}

//// GAME

// three-bit bitfield: 000-101 for 26 entries -> 130b. used for repetition of
// normalized ASCII characters in a given word
typedef uchar wordle_char_pos[17];

typedef struct {
  wordle_word word;
  wordle_char_pos cp;
} wordle_ans;

static uint wordle_ins_word(wordle_word word, uint tsize) {
  uint csize = word_list.size;

  if ((csize + 1) >= tsize) {
    tsize += 1024;
    word_list.list = realloc(word_list.list, tsize*5);
  }

  memcpy((word_list.list + csize), word, 5);

  word_list.size++;

  return tsize;
}

static void wordle_read_wordlist(int wordsfd) {
  uint buf_read = 0;

  wordle_word word;
  uint word_i = 0;

  uint tsize = 0;

  do {
    buf_read = read(wordsfd, iobuf, 4096);

    for (uint i = 0; i < buf_read; i++) {
      if (iobuf[i] == '\n') {
        word_i = 0;
        tsize = wordle_ins_word(word, tsize);
        continue;
      }

      word[word_i] = iobuf[i];
      word_i++;
    }
  } while (buf_read == 4096);

  close(wordsfd);
}

static wordle_ans wordle_answer_get(int wordsfd) {
  wordle_ans ans = {0};

  wordle_read_wordlist(wordsfd);

  // crude, but I don't care
  srand((uint)time(NULL));

  uint idx = rand() % word_list.size;
  char* ans_str = word_list.list[idx];

  for (uint i = 0; i < 5; i++) {
    ans.word[i] = ans_str[i];
  }

  for (uint i = 0; i < 5; i++) {
    uchar n = ASCII_NORM(ans.word[i]);
    uchar am = at3(ans.cp, n);

    at3_set(ans.cp, n, ++am);
  }

  return ans;
}

typedef struct {
  wordle_word* lb;
  bool found;
  uint size;
} wordle_valid;

static wordle_valid wordle_valid_cword(wordle_valid valid, uint coff, char c) {
  wordle_word* lb = valid.lb;

  const wordle_word* glist = word_list.list;

  valid.found = false;

  for (uint rm = valid.size, ni = 0, _rm = 0;;) {
    char nc = 0;

    _rm = rm;
    rm = rm >> 1;

    nc = lb[rm][coff];

    // less than: shift right by `rm'
    if (nc < c) {
      lb += rm;

      if (_rm & 0x1) {
        rm++;
      }
    }

    // found: also find the earliest one going backwards and the latest one
    // going forwards
    if (nc == c) {
      ni = (lb - glist) + rm;

      wordle_word* lw, *uw;

      lw = &glist[ni];
      for (; lw >= valid.lb && (*lw)[coff] == c; lw--);
      lw++;

      valid.found = true;

      uw = &glist[ni];
      for (const wordle_word* ub = valid.lb + valid.size;
           uw < ub && (*uw)[coff] == c; uw++);
      uw--;

      valid.lb = lw;
      valid.size = (uw - lw) + 1;
      break;
    }

    if (_rm == 1 || valid.size == 1) {
      break;
    }
  }

  return valid;
}

static bool wordle_valid_word(wordle_word input) {
  wordle_valid valid = {.lb = word_list.list, .size = word_list.size};

  for (uint i = 0; i < 5; i++) {
    valid = wordle_valid_cword(valid, i, input[i]);

    if (!valid.found) {
      return false;
    }
  }

  return true;
}

static char* wordle_word_get(char* input, uint line) {
  char str[1];

  char fixbuf[32] = {0};
  wordle_display fixdpy = {.draw_head = fixbuf, .buf = fixbuf};

  uint letters = 0;
  bool error = false;

loop:
  for (char c;;)  {
    c = getchar();

    switch (c) {
    // DEL
    case 0x7f:
#ifndef CWORDLE_DEBUG_INPUT
      // put the cursor left (unless it's at the edge), and output a dot where
      // we were
      if (letters) {
        fixdpy = wordle_cur_move(CUR_UP_ABS, 12 - line, fixdpy);
        fixdpy = wordle_cur_move(CUR_RIGHT, 11 + (letters-1), fixdpy);

        // subs the current char
        fixdpy = wordle_draw("_", 1, fixdpy);

        if (letters != 5) {
          fixdpy = wordle_draw(".", 1, fixdpy);
        }

        wordle_display_flush(&fixdpy);

        letters--;

        fixdpy = wordle_cur_move(CUR_DOWN, 12 - line, fixdpy);
        wordle_display_flush(&fixdpy);
      }
#endif
      break;

    // RET
    case 0x0a:
      if (letters == 5) {
        goto done;
      }

    // LETTERS
    default:
      if (c >= 'a' && c <= 'z' && letters < 5) {
        letters++;

#ifndef CWORDLE_DEBUG_INPUT
        fixdpy = wordle_cur_move(CUR_UP_ABS, 12 - line, fixdpy);
        fixdpy = wordle_cur_move(CUR_RIGHT, 11 + (letters-1), fixdpy);

        wordle_display_flush(&fixdpy);

        // echo the letter out
        str[0] = c;
        wordle_display_flush_str(str, 1);
#endif
        input[letters-1] = c;

#ifndef CWORDLE_DEBUG_INPUT
        if (letters != 5) {
          str[0] = '_';
          wordle_display_flush_str(str, 1);
        }

        fixdpy = wordle_cur_move(CUR_DOWN_ABS, 12 - line, fixdpy);
        wordle_display_flush(&fixdpy);
#endif
      }
    }
  }

done:
  if (!wordle_valid_word(input)) {
#ifdef CWORDLE_DEBUG_INPUT
    printf("NO %5s\n", input);
    letters = 0;
#else
    fixdpy = wordle_cur_move(CUR_UP_ABS, 1, fixdpy);
    fixdpy = wordle_draw(S("NO "), fixdpy);
    fixdpy = wordle_draw(input, 5, fixdpy);
    wordle_display_flush(&fixdpy);

    fixdpy = wordle_cur_move(CUR_DOWN_ABS, 1, fixdpy);
    wordle_display_flush(&fixdpy);
#endif

    error = true;
    goto loop;
  }

  if (error) {
#ifndef CWORDLE_DEBUG_INPUT
    fixdpy = wordle_cur_move(CUR_UP_ABS, 1, fixdpy);
    fixdpy = wordle_draw(S("\e[J"), fixdpy);;
    fixdpy = wordle_cur_move(CUR_DOWN_ABS, 1, fixdpy);

    wordle_display_flush(&fixdpy);
#endif
  }

#ifdef CWORDLE_DEBUG_INPUT
  printf("YES %5s\n", input);
  letters = 0;
  error = false;
  goto loop;
#endif

  return input;
}

typedef struct {
  char c;
  wordle_chk t;
} wordle_chk_word[5];

// two bit bitfield: 00-11 for 26 entries -> 42b
typedef uchar wordle_chk_dpy[6];

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
wordle_display_chk(wordle_chk_word word_chk, uint line, wordle_display dpy) {
  char cbuf[1];

  enum {
    NONE,
    GREEN,
    YELLOW,
    BLACK,
  } lc = NONE;

  dpy = wordle_cur_move(CUR_UP_ABS, 12 - line, dpy);
  dpy = wordle_cur_move(CUR_RIGHT, 11, dpy);

  for (uint i = 0; i < 5; i++) {
    switch (word_chk[i].t) {
    case WORDLE_CHAR_IN:
      if (lc != GREEN) {
        dpy = wordle_draw_color(S(COL_GREEN_BG), !i, dpy);
      }

      lc = GREEN;
      goto draw_c;

    case WORDLE_CHAR_OTHER:
      if (lc != YELLOW) {
        dpy = wordle_draw_color(S(COL_YELLOW_BG), !i, dpy);
      }

      lc = YELLOW;
      goto draw_c;

    case WORDLE_CHAR_NOT:
      if (lc != BLACK) {
        dpy = wordle_draw_color(S(COL_BLACK_BG), !i, dpy);
      }

      lc = BLACK;
      goto draw_c;

draw_c:
      cbuf[0] = word_chk[i].c;

      dpy = wordle_draw(cbuf, 1, dpy);
      break;
    }
  }

  dpy = wordle_draw_color(S(COL_NONE), false, dpy);

  if (line != 5) {
    dpy = wordle_cur_move(CUR_LEFT, 5, dpy);
  }

  dpy = wordle_cur_move(CUR_DOWN, 1, dpy);

  if (line != 5) {
    dpy = wordle_draw("_", 1, dpy);
  }

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
  dpy = wordle_cur_move(CUR_DOWN, 6 - line, dpy);

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

  dpy = wordle_cur_move(CUR_DOWN_ABS, 2, dpy);

  wordle_display_flush(&dpy);

  return dpy;
}

static inline wordle_chk
wordle_word_check_fixrep(wordle_char_pos cpos, uchar nc, wordle_chk_word chk,
                         char* input, char c, wordle_chk exp) {
  uint _i = at3(cpos, nc);

  // we already have it, without any ambiguity
  if (!_i) {
    return WORDLE_CHAR_NOT;
  }

  if (chk[_i - 1].t != WORDLE_CHAR_IN) {
    chk[_i - 1].t = WORDLE_CHAR_NOT;
  }
  else { // if (exp != WORDLE_CHAR_IN) {
    return WORDLE_CHAR_NOT;
  }

  for (uint j = _i; j < 5; j++) {
    if (input[j] == c && chk[j].t != WORDLE_CHAR_IN) {
      at3_set(cpos, nc, j+1);
      return exp;
    }
  }

  at3_set(cpos, nc, 0);

  return exp;
}

static bool
wordle_word_check(char* input, wordle_ans ans, uint line,
                  wordle_chk_dpy chk_dpy) {
  wordle_chk_word chk = {0};
  wordle_char_pos cpos = {0}, _ans = {0};

  // copy `ans.cp' so that we can use it for analysis
  memcpy(_ans, ans.cp, sizeof(_ans));

  bool win = true;

  // iterate `input' answer and generate a check status for each of its
  // characters, also updating the keyboard in the process
  for (uint i = 0; i < 5; i++) {
    char c = input[i], nc = ASCII_NORM(c);
    chk[i].c = c;

    wordle_chk cc = WORDLE_CHAR_NOT;
    uint _ans_am = at3(_ans, nc);

    if (ans.word[i] == c) {
      cc = WORDLE_CHAR_IN;

      // we can't have this match: actually we can, fuck whoever said we can't
      if (!_ans_am) {
        cc = wordle_word_check_fixrep(cpos, nc, chk, input, c, cc);
      }

      // we can still have this match: count down
      else {
        at3_set(_ans, nc, _ans_am - 1);
      }

      goto mark;
    }

    // `c' appears in `ans' in another position: flag current match
    if (at3(ans.cp, nc)) {
      cc = WORDLE_CHAR_OTHER;

      // we shouldn't have more than this, go to `cpos', then remove the leading
      // match, and set whatever else as its match instead
      if (!_ans_am) {
        cc = wordle_word_check_fixrep(cpos, nc, chk, input, c, cc);
        goto mark;
      }

      // we can still have this match: count down
      at3_set(_ans, nc, _ans_am - 1);

      // only set on first match
      if (!at3(cpos, nc)) {
        at3_set(cpos, nc, i + 1);
      }
    }

mark:
    chk[i].t = cc;

    win = win && (cc == WORDLE_CHAR_IN);

    wordle_chk dc = at2(chk_dpy, nc);

    // we use `1 + wordle_chk', so that we can differentiate between _NOT and
    // some key that was never pressed
    if (!dc || MAX(dc - 1, cc) == cc) {
      at2_set(chk_dpy, nc, cc + 1);
    }
  }

  wordle_display dpy = {.buf = buf, .draw_head = buf, .size = 0};

  dpy = wordle_display_chk(chk, line, dpy);
  dpy = wordle_display_kbd(chk_dpy, line, dpy);

  return win;
}

static void wordle_game(wordle_ans ans) {
  wordle_word input = {0};

  wordle_chk_dpy chk_dpy = {0};
  wordle_display edpy = {.buf = buf, .draw_head = buf, .size = 0};

  bool won = false;
  char tries[1];

  uint try = 0;

  for (try = 0; try < 6; try++) {
    wordle_word_get(input, try);

    won = wordle_word_check(input, ans, try, chk_dpy);

    if (won) {
      edpy = wordle_cur_move(CUR_UP_ABS, 1, edpy);

      // OK plane 1/6
      edpy = wordle_draw(S("OK "), edpy);
      edpy = wordle_draw(ans.word, 5, edpy);
      edpy = wordle_draw(" ", 1, edpy);
      edpy = wordle_draw((tries[0] = try + 49, tries), 1, edpy);
      edpy = wordle_draw(S("/6\n"), edpy);

      wordle_display_flush(&edpy);

      break;
    }

    if (try == 5) {
      edpy = wordle_cur_move(CUR_UP_ABS, 1, edpy);

      // FAIL plane
      edpy = wordle_draw(S("FAIL "), edpy);
      edpy = wordle_draw(ans.word, 5, edpy);
      edpy = wordle_draw(S("\n"), edpy);

      wordle_display_flush(&edpy);

      break;
    }
  }

  wordle_term_exit();
  exit(0);
}

//// HELPERS

static inline int help(void) {
  puts("Usage:       cwordle WORD-LIST\n"
"Description: Wordle clone written in C. Will init a game of wordle given the\n"
"             WORD-LIST. It must be a list of 5-char words separated by\n"
"             newlines");

  return 0;
}

//// DISPLAY

static wordle_display wordle_display_setup(wordle_display dpy) {
  dpy.size = 0;
  dpy.buf = dpy.draw_head = buf;

  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           ....."), dpy);
  dpy = wordle_draw(LN("           .....\n"), dpy);

  dpy = wordle_draw(LN("     q w e r t y u i o p"), dpy);
  dpy = wordle_draw(LN("      a s d f g h j k l"), dpy);
  dpy = wordle_draw(LN("        z x c v b n m\n\n"), dpy);

  dpy = wordle_cur_move(CUR_UP, 12, dpy);
  dpy = wordle_cur_move(CUR_RIGHT, 11, dpy);
  dpy = wordle_draw(S("_"), dpy);

  wordle_display_flush(&dpy);

  dpy = wordle_cur_move(CUR_DOWN_ABS, 12, dpy);
  wordle_display_flush(&dpy);

  return dpy;
}

//// MAIN GAME

static int wordle_main(int wordsfd) {
  if (wordsfd <= 0) {
    puts("[ !! ] Missing word list. See `--help'.");
    return 1;
  }

  wordle_ans ans = wordle_answer_get(wordsfd);
  wordle_display dpy = {.buf = buf};

#ifdef CWORDLE_DEBUG_ANS
  printf("ans: %s\n", ans.word);
#endif

  wordle_term_init();

#ifndef CWORDLE_DEBUG_INPUT
  dpy = wordle_display_setup(dpy);
#endif

  wordle_game(ans);

  return 0;
}

////

int main(int argc, char** argv) {
  int wordsfd = 0;

  for (uint i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      return help();
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
