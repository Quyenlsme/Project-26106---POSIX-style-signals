// user/ctrlc_demo.c — Demo 1 (DL-11): Ctrl+C gửi SIGINT tới foreground process
// Dự án 26106 — POSIX-style signals trên xv6-riscv
// Người viết: Nguyễn Trung Kiên — Tuần 4
//
// Cách dùng:
//   ctrlc_demo      -> đăng ký handler SIGINT, bắt Ctrl+C rồi thoát sạch
//   ctrlc_demo dfl  -> giữ SIG_DFL, Ctrl+C kết thúc tiến trình mặc định

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

static volatile int stop = 0;

static void
on_sigint(int sig)
{
  stop = 1;
  printf("[DEMO 1] Caught Ctrl+C (SIGINT)! Cleaning up and exiting...\n");
}

int
main(int argc, char *argv[])
{
  int use_default = 0;

  if (argc == 2 && strcmp(argv[1], "dfl") == 0) {
    use_default = 1;
  } else if (argc != 1) {
    printf("Usage: ctrlc_demo [dfl]\n");
    exit(1);
  }

  if (use_default) {
    printf(
      "[DEMO 1] PID %d running with SIG_DFL (press Ctrl+C to terminate)...\n",
      getpid());
    for (;;) {
      printf("Running...\n");
      pause(10);
    }
  }

  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = on_sigint;
  act.sa_restorer = sigrestorer;
  if (sigaction(SIGINT, &act, 0) < 0) {
    printf("ctrlc_demo: sigaction failed\n");
    exit(1);
  }

  printf(
    "[DEMO 1] PID %d running (press Ctrl+C to trigger SIGINT handler)...\n",
    getpid());
  while (!stop) {
    printf("Running...\n");
    pause(10);
  }

  exit(0);
}
