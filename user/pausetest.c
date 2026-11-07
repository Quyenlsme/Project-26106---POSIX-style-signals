// user/pausetest.c — Kiểm thử semantics pause(n) khi có signal (PA-01..PA-06)
// Dự án 26106 — POSIX-style signals trên xv6-riscv
// Người viết: Nguyễn Trung Kiên — Tuần 3
//
// pause(n) là syscall có sẵn của xv6 (ngủ n ticks). Sau tuần 2, vòng chờ
// trong sys_pause() gọi sig_interrupted(), nên pause trả -1 khi có signal
// deliverable (không bị block, không SIG_IGN).

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

static int passed = 0;
static int failed = 0;
static volatile int alrm_count = 0;
static volatile int usr1_count = 0;

static void
check(int cond, char *name)
{
  if (cond) {
    printf("  [PASS] %s\n", name);
    passed++;
  } else {
    printf("  [FAIL] %s\n", name);
    failed++;
  }
}

static void
on_alrm(int sig)
{
  alrm_count++;
}

static void
on_usr1(int sig)
{
  usr1_count++;
}

static void
set_handler(int sig, void (*h)(int))
{
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = h;
  act.sa_restorer = sigrestorer;
  if (sigaction(sig, &act, 0) < 0) {
    printf("pausetest: sigaction(%d) failed\n", sig);
    exit(1);
  }
}

// PA-01: pause(0) trả về 0 ngay
static void
test_pause_zero(void)
{
  printf("PA-01: pause(0)\n");
  check(pause(0) == 0, "pause(0) == 0");
}

// PA-02: SIGALRM có handler ngắt pause dài -> -1, handler chạy 1 lần
static void
test_pause_alarm(void)
{
  printf("PA-02: alarm(5) ngat pause(1000)\n");
  set_handler(SIGALRM, on_alrm);
  alrm_count = 0;
  int t0 = uptime();
  alarm(5);
  int r = pause(1000);
  int dt = uptime() - t0;
  check(r == -1, "pause tra ve -1 khi bi signal ngat");
  check(alrm_count == 1, "handler SIGALRM chay dung 1 lan");
  check(dt < 100, "thuc day som (khong ngu het 1000 ticks)");
}

// PA-03: SIGALRM bị block -> không ngắt pause; unblock -> handler chạy
static void
test_pause_blocked(void)
{
  printf("PA-03: SIGALRM bi block khong ngat pause\n");
  set_handler(SIGALRM, on_alrm);
  alrm_count = 0;
  uint set = 1U << SIGALRM;
  sigprocmask(SIG_BLOCK, &set, 0);
  int t0 = uptime();
  alarm(3);
  int r = pause(10);
  int dt = uptime() - t0;
  check(r == 0, "pause(10) tra ve 0 (signal bi block)");
  check(dt >= 10, "ngu du 10 ticks");
  check(alrm_count == 0, "handler chua chay khi dang block");
  sigprocmask(SIG_UNBLOCK, &set, 0);
  check(alrm_count == 1, "handler chay ngay sau unblock");
}

// PA-04: SIGUSR1 = SIG_IGN không ngắt pause
static void
test_pause_ignored(void)
{
  printf("PA-04: SIG_IGN khong ngat pause\n");
  set_handler(SIGUSR1, SIG_IGN);
  int me = getpid();
  int pid = fork();
  if (pid == 0) {
    pause(3);
    kill(me, SIGUSR1);
    exit(0);
  }
  int r = pause(15);
  wait(0);
  check(r == 0, "pause(15) tra ve 0 (signal bi ignore)");
}

// PA-05: signal từ tiến trình khác (có handler) ngắt pause
static void
test_pause_other_proc(void)
{
  printf("PA-05: kill tu tien trinh khac ngat pause\n");
  set_handler(SIGUSR1, on_usr1);
  usr1_count = 0;
  int me = getpid();
  int pid = fork();
  if (pid == 0) {
    pause(3);
    kill(me, SIGUSR1);
    exit(0);
  }
  int r = pause(1000);
  wait(0);
  check(r == -1, "pause tra ve -1");
  check(usr1_count == 1, "handler SIGUSR1 chay dung 1 lan");
}

// PA-06: "chờ vô hạn tới khi có signal" — dạng POSIX pause()
static void
test_pause_forever(void)
{
  printf("PA-06: pause(0x7fffffff) cho toi khi co signal\n");
  set_handler(SIGALRM, on_alrm);
  alrm_count = 0;
  alarm(5);
  int r = pause(0x7fffffff);
  check(r == -1 && alrm_count == 1, "thuc day boi SIGALRM, tra ve -1");
}

int
main(int argc, char *argv[])
{
  printf("\n=== PAUSETEST - pause + signal ===\n");
  test_pause_zero();
  test_pause_alarm();
  test_pause_blocked();
  test_pause_ignored();
  test_pause_other_proc();
  test_pause_forever();
  printf("=== Ket qua: %d PASS, %d FAIL ===\n", passed, failed);
  if (failed == 0)
    printf("pausetest: ALL PASSED\n");
  exit(failed == 0 ? 0 : 1);
}
