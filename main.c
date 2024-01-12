// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

// name -> program
struct {
  char *name;
  int (*fn)(Prog *, char *, char **, int);
} tab[] = {
    {"recursive", recursiveprog},
    {"recursiveloop", recursiveloopprog},
    {"backtrack", backtrack},
    {"thompson", thompsonvm},
    {"pike", pikevm},
};

// usgae
void usage(void) {
  fprintf(stderr, "usage: re regexp string...\n");
  exit(2);
}

int main(int argc, char **argv) {
  int i, j, k, l;
  Regexp *re;
  Prog *prog;
  char *sub[MAXSUB];

  if (argc < 2) usage();

  // parse regexp string into regexp data structure
  re = parse(argv[1]);
  printre(re);
  printf("\n");

  // compile regexp to VM instructions
  prog = compile(re);
  printprog(prog);

  for (i = 2; i < argc; i++) {
    printf("================ input string: #%d %s\n", i - 1, argv[i]);
    for (j = 0; j < (int)nelem(tab); j++) {
      printf("[%s] ", tab[j].name);
      memset(sub, 0, sizeof sub);
      if (!tab[j].fn(prog, argv[i], sub, nelem(sub))) {  // not match
        printf("-no match-\n");
        continue;
      }

      printf("match");
      for (k = MAXSUB; k > 0; k--)
        if (sub[k - 1]) break;

      // display all submatches
      for (l = 0; l < k; l += 2) {
        printf(" [");
        if (sub[l] == nil)
          printf("?");
        else
          printf("%d", (int)(sub[l] - argv[i]));
        printf(",");
        if (sub[l + 1] == nil)
          printf("?");
        else
          printf("%d", (int)(sub[l + 1] - argv[i]));
        printf(")");
      }
      printf("\n");
    }
  }
  return 0;
}
