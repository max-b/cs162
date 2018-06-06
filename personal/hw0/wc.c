#include <stdio.h>
#include <stdlib.h>

void print_help() {
  printf("Must supply 1 argument\n");
}

struct count {
  int result_code;
  int words;
  int lines;
  int characters;
};

struct count word_count(char *filename) {
  FILE *f;
  int c;
  struct count result;
  int in_word = 0;

  f = fopen(filename, "r");
  if (!f) {
    printf("Error opening file: %s\n", filename);
    result.result_code = -1;
    return result;
  }

  result.words = 0;
  result.lines = 0;
  result.characters = 0;
  do {
    c = fgetc(f);
    if (c >= 33 && c <= 126) {
      if (!in_word) {
        in_word = 1;
      }
    } else if (in_word) {
      in_word = 0;
      result.words++;
    }

    if (c == 10) {
      result.lines++;
    }

    if (c != EOF) {
      result.characters++;
    }

  } while (c != EOF);

  result.result_code = 0;
  return result;
}

int main(int argc, char *argv[]) {
  struct count result;
  if (argc != 2) {
    print_help();
    exit(1);
  } else {
    result = word_count(argv[1]);
    if (result.result_code == 0) {
      printf("  %d  %d %d %s\n", result.lines, result.words, result.characters, argv[1]);
    }
  }
  return result.result_code;
}
