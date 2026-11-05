// user/alarm_demo.c — Demo 2: alarm() kích hoạt handler định kỳ (re-arm)
// Dự án 26106 — POSIX-style signals trên xv6-riscv
// Người viết: Nguyễn Trung Kiên — Tuần 3
//
// alarm() là one-shot; tính định kỳ có được do handler tự gọi lại alarm().

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

#define PERIOD 10 // ticks (~1 giây trên QEMU mặc định)
#define ROUNDS 5

static volatile int fired = 0;

static void
on_alarm(int sig)
{
  fired++;
  printf("[DEMO 2] SIGALRM #%d at uptime=%d\n", fired, uptime());
  if (fired < ROUNDS)
    alarm(PERIOD); // re-arm
}

int
main(int argc, char *argv[])
{
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = on_alarm;
  act.sa_restorer = sigrestorer;
  if (sigaction(SIGALRM, &act, 0) < 0) {
    printf("alarm_demo: sigaction failed\n");
    exit(1);
  }

  int start = uptime();
  printf("[DEMO 2] start at uptime=%d, period=%d ticks, rounds=%d\n", start,
         PERIOD, ROUNDS);
  alarm(PERIOD);

  int interrupted = 0;
  while (fired < ROUNDS) {
    // Ngủ chờ signal. Timeout giới hạn (3*PERIOD) để tránh race kinh điển:
    // signal cuối có thể đến giữa lúc kiểm tra fired và lúc gọi pause.
    if (pause(PERIOD * 3) == -1)
      interrupted++;
  }

  printf("[DEMO 2] done: %d alarms, pause interrupted %d times, elapsed=%d "
         "ticks\n",
         fired, interrupted, uptime() - start);
  exit(0);
}
