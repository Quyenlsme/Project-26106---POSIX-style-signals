// user/killtest.c — Unit test cho module signal core (kill / sig_send)
// Dự án 26106 — POSIX-style signals trên xv6-riscv
// Người viết: Phạm Lê Quyền — Tuần 2
//
// Phạm vi:
//   - kill() kiểm tra tham số (sig, pid) -> -1 khi sai
//   - SIGKILL kết thúc tiến trình con đang chạy / đang ngủ, exit status = -1
//   - Signal thường với SIG_DFL -> hành động mặc định kết thúc tiến trình (-1)
//   - sig_send đánh thức tiến trình đang ngủ khi nhận signal thường

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/signal.h"
#include "user/user.h"

static int passed = 0;
static int failed = 0;

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
spin(int n)
{
  volatile int i;
  for (i = 0; i < n; i++)
    ;
}

// Test 1: số hiệu signal không hợp lệ
static void
test_invalid_sig(void)
{
  printf("Test 1: sig khong hop le\n");
  int me = getpid();
  check(kill(me, 0) == -1, "kill(self, 0) == -1");
  check(kill(me, -1) == -1, "kill(self, -1) == -1");
  check(kill(me, NSIG) == -1, "kill(self, NSIG) == -1");
  check(kill(me, 1000) == -1, "kill(self, 1000) == -1");
}

// Test 2: pid không hợp lệ / không tồn tại
static void
test_invalid_pid(void)
{
  printf("Test 2: pid khong hop le\n");
  check(kill(99999, SIGUSR1) == -1, "kill(99999, SIGUSR1) == -1");
  check(kill(0, SIGUSR1) == -1, "kill(0, SIGUSR1) == -1");
  check(kill(-5, SIGUSR1) == -1, "kill(-5, SIGUSR1) == -1");
}

// Test 3: SIGKILL kết thúc tiến trình con đang chạy vòng lặp
static void
test_sigkill_running(void)
{
  printf("Test 3: SIGKILL tien trinh dang chay\n");
  int pid = fork();
  if (pid < 0) {
    check(0, "fork");
    return;
  }
  if (pid == 0) {
    for (;;)
      spin(1000);
  }
  spin(1000000);
  check(kill(pid, SIGKILL) == 0, "kill(child, SIGKILL) == 0");
  int status = 0;
  int w = wait(&status);
  check(w == pid, "wait() tra ve dung pid con");
  check(status == -1, "exit status cua con == -1");
}

// Test 4: SIGKILL đánh thức và kết thúc tiến trình con đang ngủ (block trên read pipe)
static void
test_sigkill_sleeping(void)
{
  printf("Test 4: SIGKILL tien trinh dang ngu (block tren read)\n");
  int ready[2], block[2];
  if (pipe(ready) < 0 || pipe(block) < 0) {
    check(0, "pipe");
    return;
  }
  int pid = fork();
  if (pid < 0) {
    check(0, "fork");
    return;
  }
  if (pid == 0) {
    char c;
    close(ready[0]);
    close(block[1]);
    write(ready[1], "r", 1);
    read(block[0], &c, 1); // không ai ghi -> ngủ mãi nếu không bị đánh thức
    exit(0);
  }
  close(ready[1]);
  close(block[0]);
  char c;
  read(ready[0], &c, 1); // đợi con báo sẵn sàng
  spin(1000000);         // cho con kịp vào read() và ngủ
  check(kill(pid, SIGKILL) == 0, "kill(child dang ngu, SIGKILL) == 0");
  int status = 0;
  int w = wait(&status);
  check(w == pid, "wait() tra ve (con duoc danh thuc)");
  check(status == -1, "exit status cua con == -1");
  close(ready[0]);
  close(block[1]);
}

// Test 5: signal thường (SIGUSR1) với SIG_DFL -> hành động mặc định kết thúc tiến trình
static void
test_default_action(void)
{
  printf("Test 5: SIGUSR1 (SIG_DFL) ket thuc tien trinh con\n");
  int pid = fork();
  if (pid < 0) {
    check(0, "fork");
    return;
  }
  if (pid == 0) {
    for (;;)
      spin(1000);
  }
  spin(1000000);
  check(kill(pid, SIGUSR1) == 0, "kill(child, SIGUSR1) == 0");
  int status = 0;
  int w = wait(&status);
  check(w == pid, "wait() tra ve dung pid con");
  check(status == -1, "exit status cua con == -1 (default action)");
}

// Test 6: signal thường đánh thức tiến trình đang ngủ (kiểm tra wakeup trong sig_send)
static void
test_signal_wakes_sleeper(void)
{
  printf("Test 6: SIGTERM danh thuc va ket thuc tien trinh dang ngu\n");
  int ready[2], block[2];
  if (pipe(ready) < 0 || pipe(block) < 0) {
    check(0, "pipe");
    return;
  }
  int pid = fork();
  if (pid < 0) {
    check(0, "fork");
    return;
  }
  if (pid == 0) {
    char c;
    close(ready[0]);
    close(block[1]);
    write(ready[1], "r", 1);
    read(block[0], &c, 1);
    exit(0);
  }
  close(ready[1]);
  close(block[0]);
  char c;
  read(ready[0], &c, 1);
  spin(1000000);
  check(kill(pid, SIGTERM) == 0, "kill(child dang ngu, SIGTERM) == 0");
  int status = 0;
  int w = wait(&status);
  check(w == pid, "wait() tra ve (con duoc danh thuc)");
  check(status == -1, "exit status cua con == -1");
  close(ready[0]);
  close(block[1]);
}

int
main(int argc, char *argv[])
{
  printf("\n=== KILLTEST - signal core ===\n");

  test_invalid_sig();
  test_invalid_pid();
  test_sigkill_running();
  test_sigkill_sleeping();
  test_default_action();
  test_signal_wakes_sleeper();

  printf("=== Ket qua: %d PASS, %d FAIL ===\n", passed, failed);
  if (failed == 0)
    printf("killtest: ALL PASSED\n");
  exit(failed == 0 ? 0 : 1);
}
