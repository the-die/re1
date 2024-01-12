// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

/*
 * In Thompson's VM, addthread could limit the size of the thread lists to n,
 * the length of the compiled program, by keeping only one thread with each
 * possible PC. In Pike's VM, the thread state is larger—it includes the saved
 * pointers too—but addthread can still keep just one thread with each possible
 * PC. This is because the saved pointers do not influence future execution:
 * they only record past execution. Two threads with the same PC will execute
 * identically even if they have different saved pointers; thus only one thread
 * per PC needs to be kept.
 *
 * -----------------------------------------------------------------------------
 *
 * To make it respect priority, we can make addthread handle Jmp, Split, and
 * Save instructions by calling itself recursively to add the targets of those
 * instructions instead. This change ensures that clist and nlist are maintained
 * in order of thread priority, from highest to lowest. The processing loop in
 * pikevm thus tries threads in priority order, and the aggressive addthread
 * makes sure that all threads generated from one priority level are added to
 * nlist before considering threads from the next priority level.
 *
 * The pikevm changes are motivated by the observation that recursion respects
 * thread priority. The new code uses recursion while processing a single
 * character, so that nlist will be generated in priority order, but it still
 * advances threads in lock step to keep the good run-time behavior. Because
 * nlist is generated in priority order, the “ignore a thread if the PC has been
 * seen before” heuristic is safe: the thread seen earlier is higher priority
 * and should be the one that gets saved.
 *
 * There is one more change necessary in pikevm: if a match is found, threads
 * that occur later in the clist (lower-priority ones) should be cut off, but
 * higher-priority threads need to be given the chance to match possibly-longer
 * sections of the string.
 */

#include "regexp.h"

typedef struct Thread Thread;
struct Thread {
  Inst *pc;
  Sub *sub;
};

typedef struct ThreadList ThreadList;
struct ThreadList {
  int n;
  Thread t[1];
};

static Thread thread(Inst *pc, Sub *sub) {
  Thread t = {pc, sub};
  return t;
}

static ThreadList *threadlist(int n) {
  return mal(sizeof(ThreadList) + n * sizeof(Thread));
}

static void addthread(ThreadList *l, Thread t, char *sp) {
  if (t.pc->gen == gen) {
    decref(t.sub);
    return;  // already on list
  }
  t.pc->gen = gen;

  switch (t.pc->opcode) {
    default:
      l->t[l->n] = t;
      l->n++;
      break;
    case Jmp:
      addthread(l, thread(t.pc->x, t.sub), sp);
      break;
    case Split:
      addthread(l, thread(t.pc->x, incref(t.sub)), sp);
      addthread(l, thread(t.pc->y, t.sub), sp);
      break;
    case Save:
      addthread(l, thread(t.pc + 1, update(t.sub, t.pc->n, sp)), sp);
      break;
  }
}

int pikevm(Prog *prog, char *input, char **subp, int nsubp) {
  int i, len;
  ThreadList *clist, *nlist, *tmp;
  Inst *pc;
  char *sp;
  Sub *sub, *matched;

  matched = nil;
  for (i = 0; i < nsubp; i++) subp[i] = nil;
  sub = newsub(nsubp);
  for (i = 0; i < nsubp; i++) sub->sub[i] = nil;

  len = prog->len;
  clist = threadlist(len);
  nlist = threadlist(len);

  gen++;
  addthread(clist, thread(prog->start, sub), input);
  matched = 0;
  for (sp = input;; sp++) {
    if (clist->n == 0) break;
    // printf("%d(%02x).", (int)(sp - input), *sp & 0xFF);
    gen++;
    for (i = 0; i < clist->n; i++) {
      pc = clist->t[i].pc;
      sub = clist->t[i].sub;
      // printf(" %d", (int)(pc - prog->start));
      switch (pc->opcode) {
        case Char:
          if (*sp != pc->c) {
            decref(sub);
            break;
          }
          __attribute__((fallthrough));
        case Any:
          if (*sp == 0) {
            decref(sub);
            break;
          }
          addthread(nlist, thread(pc + 1, sub), sp + 1);
          break;
        case Match:
          if (matched) decref(matched);
          matched = sub;
          for (i++; i < clist->n; i++) decref(clist->t[i].sub);
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
  if (matched) {
    for (i = 0; i < nsubp; i++) subp[i] = matched->sub[i];
    decref(matched);
    return 1;
  }
  return 0;
}
