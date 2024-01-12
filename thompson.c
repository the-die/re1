// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

/*
 * Thompson observed that backtracking required scanning some parts of the input
 * string multiple times. To avoid this, he built a VM implementation that ran
 * all the threads in lock step: they all process the first character in the
 * string, then they all process the second, and so on. This is possible because
 * newly created VM threads never look backward in the string, so they can be
 * coerced into lock step with the existing threads.
 *
 * Because all threads execute in lock step, they all have the same value for
 * the string pointer, so it is no longer necessary to save as part of the
 * thread state.
 */

#include "regexp.h"

typedef struct Thread Thread;
struct Thread {
  Inst *pc;
};

typedef struct ThreadList ThreadList;
struct ThreadList {
  int n;
  Thread t[1];
};

static Thread thread(Inst *pc) {
  Thread t = {pc};
  return t;
}

static ThreadList *threadlist(int n) {
  return mal(sizeof(ThreadList) + n * sizeof(Thread));
}

static void addthread(ThreadList *l, Thread t) {
  if (t.pc->gen == gen) return;  // already on list

  t.pc->gen = gen;
  l->t[l->n] = t;
  l->n++;

  switch (t.pc->opcode) {
    case Jmp:
      addthread(l, thread(t.pc->x));
      break;
    case Split:
      addthread(l, thread(t.pc->x));
      addthread(l, thread(t.pc->y));
      break;
    case Save:
      addthread(l, thread(t.pc + 1));
      break;
  }
}

/*
 * Suppose that there are n instructions in the regular expression program being
 * run. Because the thread state is only the program counter, there are only n
 * different possible threads that can appear on clist or nlist. If addthread
 * does not add a thread to the list if an identical thread (with the same pc)
 * is already on the list, then ThreadLists only need room for n possible
 * threads, eliminating the possibility of overflow.
 */

int thompsonvm(Prog *prog, char *input, char **subp, int nsubp) {
  int i, len, matched;
  ThreadList *clist, *nlist, *tmp;
  Inst *pc;
  char *sp;

  for (i = 0; i < nsubp; i++) subp[i] = nil;

  len = prog->len;
  clist = threadlist(len);  // current
  nlist = threadlist(len);  // next

  if (nsubp >= 1) subp[0] = input;
  gen++;
  addthread(clist, thread(prog->start));
  matched = 0;
  for (sp = input;; sp++) {
    if (clist->n == 0) break;
    // printf("%d(%02x).", (int)(sp - input), *sp & 0xFF);
    gen++;
    for (i = 0; i < clist->n; i++) {
      pc = clist->t[i].pc;
      // printf(" %d", (int)(pc - prog->start));
      switch (pc->opcode) {
        case Char:
          if (*sp != pc->c) break;
          __attribute__((fallthrough));
        case Any:
          if (*sp == 0) break;
          addthread(nlist, thread(pc + 1));
          break;
        case Match:
          if (nsubp >= 2) subp[1] = sp;
          matched = 1;
          goto BreakFor;
          // Jmp, Split, Save handled in addthread, so that
          // machine execution matches what a backtracker would do.
          // This is discussed (but not shown as code) in
          // Regular Expression Matching: the Virtual Machine Approach.
      }
    }
  BreakFor:
    // printf("\n");
    tmp = clist;
    clist = nlist;
    nlist = tmp;
    nlist->n = 0;
    if (*sp == '\0') break;
  }
  return matched;
}
