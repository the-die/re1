// Copyright 2007-2009 Russ Cox.  All Rights Reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "regexp.h"

// We can avoid overflowing the C stack by maintaining an explicit thread stack
// instead.
typedef struct Thread Thread;
struct Thread {
  Inst *pc;
  char *sp;
  Sub *sub;
};

static Thread thread(Inst *pc, char *sp, Sub *sub) {
  Thread t = {pc, sp, sub};
  return t;
}

/*
 * Then the VM implementation repeatedly takes a thread off its ready list and
 * runs it to completion. If one thread finds a match, we can stop early: the
 * remaining threads need not be run. If all threads finish without finding a
 * match, there is no match. We impose a simple limit on the number of threads
 * waiting to run, reporting an error if the limit is reached.
 */
int backtrack(Prog *prog, char *input, char **subp, int nsubp) {
  enum { MAX = 1000 };
  Thread ready[MAX];
  int i, nready;
  Inst *pc;
  char *sp;
  Sub *sub;

  /* queue initial thread */
  sub = newsub(nsubp);
  for (i = 0; i < nsubp; i++) sub->sub[i] = nil;
  ready[0] = thread(prog->start, input, sub);
  nready = 1;

  /* run threads in stack order */
  while (nready > 0) {
    --nready; /* pop state for next thread to run */
    pc = ready[nready].pc;
    sp = ready[nready].sp;
    sub = ready[nready].sub;
    assert(sub->ref > 0);
    for (;;) {
      switch (pc->opcode) {
        case Char:
          if (*sp != pc->c) goto Dead;
          pc++;
          sp++;
          continue;
        case Any:
          if (*sp == 0) goto Dead;
          pc++;
          sp++;
          continue;
        case Match:
          for (i = 0; i < nsubp; i++) subp[i] = sub->sub[i];
          decref(sub);
          return 1;
        case Jmp:
          pc = pc->x;
          continue;
        case Split:
          if (nready >= MAX) fatal("backtrack overflow");
          ready[nready++] = thread(pc->y, sp, incref(sub));
          pc = pc->x; /* continue current thread */
          continue;
        case Save:
          sub = update(sub, pc->n, sp);
          pc++;
          continue;
      }
    }
  Dead:
    decref(sub);
  }
  return 0;
}
