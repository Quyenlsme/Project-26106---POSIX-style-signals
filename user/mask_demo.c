// user/mask_demo.c — Demo 3: signal bị mask được giữ pending, delivery sau unmask
// Dự án 26106 — POSIX-style signals trên xv6-riscv
// Người viết: Nguyễn Trung Kiên — Tuần 3

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

static volatile int got = 0;

static void
on_usr1(int sig)
{
  got++;
  printf("[DEMO 3] handler: caught SIGUSR1 (count=%d)\n", got);
}

int
main(int argc, char *argv[])
{
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = on_usr1;
  act.sa_restorer = sigrestorer;
  if (sigaction(SIGUSR1, &act, 0) < 0) {
    printf("mask_demo: sigaction failed\n");
    exit(1);
  }

  uint set = 1U << SIGUSR1;
  sigprocmask(SIG_BLOCK, &set, 0);
  printf("[DEMO 3] SIGUSR1 blocked\n");

  int me = getpid();
  int pid = fork();
  if (pid < 0) {
    printf("mask_demo: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    for (int i = 0; i < 3; i++)
      kill(me, SIGUSR1);
    exit(0);
  }
  wait(0);

  printf("[DEMO 3] child sent SIGUSR1 x3; handler count=%d (expect 0)\n", got);
  printf("[DEMO 3] working while signal is pending...\n");
  pause(10);
  printf("[DEMO 3] still count=%d, now unblocking SIGUSR1\n", got);

  sigprocmask(SIG_UNBLOCK, &set, 0);

  printf("[DEMO 3] after unblock: count=%d (expect 1, 3 sends coalesced)\n",
         got);
  exit(got == 1 ? 0 : 1);
}
