// user/sigtest.c — Bộ kiểm thử hạ tầng Signal cho xv6-riscv
// Kiểm tra tính đúng đắn của các syscall: sigprocmask, alarm, sigaction, kill

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

// Forward declarations
void test_sigprocmask(void);
void test_alarm(void);
void test_sigaction(void);
void test_kill(void);
void test_mask_pending_delivery(void);

// Đặt main() ở đầu file để main() nằm tại địa chỉ 0x0 trong ELF (chuẩn xv6 user program)
int
main(int argc, char *argv[])
{
  printf("\n========================================\n");
  printf("  SIGNAL SYSTEM INTEGRATION TEST (WEEK 2)\n");
  printf("========================================\n\n");

  test_sigprocmask();
  test_alarm();
  test_sigaction();
  test_kill();
  test_mask_pending_delivery();

  printf("========================================\n");
  printf("  ALL TESTS PASSED SUCCESSFULLY!\n");
  printf("========================================\n\n");

  exit(0);
}

int handler_called = 0;

void
custom_handler(int sig)
{
  handler_called++;
  printf("  -> [Handler] Received signal %d (count=%d)\n", sig, handler_called);
}

void
test_sigprocmask(void)
{
  printf("=== 1. Test sigprocmask ===\n");

  unsigned int oldmask = 0;
  int ret;

  // Test 1.1: Chặn SIGUSR1 qua SIG_BLOCK
  unsigned int block_set = (1U << SIGUSR1);
  ret = sigprocmask(SIG_BLOCK, &block_set, &oldmask);
  if (ret < 0) {
    printf("  [FAIL] SIG_BLOCK failed\n");
    exit(1);
  }
  printf("  [PASS] SIG_BLOCK SIGUSR1 (ret=%d, oldmask=0x%x)\n", ret, oldmask);

  // Test 1.2: Đọc mask hiện tại (set = NULL)
  unsigned int cur_mask = 0;
  ret = sigprocmask(SIG_BLOCK, 0, &cur_mask);
  if (ret < 0 || (cur_mask & (1U << SIGUSR1)) == 0) {
    printf(
      "  [FAIL] Reading mask failed or SIGUSR1 not blocked (cur_mask=0x%x)\n",
      cur_mask);
    exit(1);
  }
  printf("  [PASS] Query current mask with set=NULL (mask=0x%x)\n", cur_mask);

  // Test 1.3: Cố tình chặn SIGKILL (phải bị sig_sanitize loại bỏ)
  unsigned int kill_set = (1U << SIGKILL);
  ret = sigprocmask(SIG_BLOCK, &kill_set, 0);
  sigprocmask(SIG_BLOCK, 0, &cur_mask);
  if (cur_mask & (1U << SIGKILL)) {
    printf("  [FAIL] SIGKILL was blocked! Invariant violated!\n");
    exit(1);
  }
  printf("  [PASS] Invariant: SIGKILL cannot be blocked (mask=0x%x)\n",
         cur_mask);

  // Test 1.4: Bỏ chặn SIGUSR1 qua SIG_UNBLOCK
  ret = sigprocmask(SIG_UNBLOCK, &block_set, &oldmask);
  sigprocmask(SIG_BLOCK, 0, &cur_mask);
  if (ret < 0 || (cur_mask & (1U << SIGUSR1)) != 0) {
    printf("  [FAIL] SIG_UNBLOCK failed\n");
    exit(1);
  }
  printf("  [PASS] SIG_UNBLOCK SIGUSR1 (ret=%d, cur_mask=0x%x)\n", ret,
         cur_mask);

  // Test 1.5: Tham số how không hợp lệ
  ret = sigprocmask(99, &block_set, 0);
  if (ret != -1) {
    printf("  [FAIL] Invalid how=99 expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Invalid how=99 rejected with -1\n");

  printf("  => sigprocmask tests: ALL PASSED\n\n");
}

void
test_alarm(void)
{
  printf("=== 2. Test alarm ===\n");

  int ret;

  // Test 2.1: Đặt alarm lần đầu
  ret = alarm(10);
  if (ret != 0) {
    printf("  [FAIL] First alarm(10) expected 0, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] First alarm(10) returned 0\n");

  // Test 2.2: Thay thế deadline cũ, nhận lại số tick còn lại
  ret = alarm(25);
  if (ret <= 0) {
    printf("  [FAIL] Replacing alarm expected remaining > 0, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Replaced alarm(25), remaining ticks returned: %d\n", ret);

  // Test 2.3: Hủy alarm bằng alarm(0)
  ret = alarm(0);
  if (ret <= 0) {
    printf("  [FAIL] alarm(0) expected remaining > 0, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Canceled alarm(0), remaining ticks returned: %d\n", ret);

  // Test 2.4: Kiểm tra sau khi hủy, alarm(5) trả về 0
  ret = alarm(5);
  if (ret != 0) {
    printf("  [FAIL] alarm(5) after cancel expected 0, got %d\n", ret);
    exit(1);
  }
  alarm(0); // Dọn dẹp
  printf("  [PASS] Verified clean state after cancel\n");

  // Test 2.5: Tham số âm không hợp lệ
  ret = alarm(-5);
  if (ret != -1) {
    printf("  [FAIL] alarm(-5) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Negative ticks rejected with -1\n");

  printf("  => alarm tests: ALL PASSED\n\n");
}

void
test_sigaction(void)
{
  printf("=== 3. Test sigaction ===\n");

  int ret;
  struct sigaction act, oldact;

  // Test 3.1: Đăng ký SIG_IGN cho SIGUSR1
  memset(&act, 0, sizeof(act));
  act.sa_handler = SIG_IGN;
  act.sa_flags = 0;
  act.sa_restorer = 0;

  ret = sigaction(SIGUSR1, &act, &oldact);
  if (ret < 0) {
    printf("  [FAIL] sigaction(SIGUSR1, SIG_IGN) failed\n");
    exit(1);
  }
  printf("  [PASS] sigaction(SIGUSR1, SIG_IGN) succeeded\n");

  // Test 3.2: Không thể can thiệp SIGKILL
  ret = sigaction(SIGKILL, &act, 0);
  if (ret != -1) {
    printf("  [FAIL] sigaction(SIGKILL) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Invariant: SIGKILL cannot be modified\n");

  // Test 3.3: Số hiệu signal không hợp lệ
  ret = sigaction(0, &act, 0);
  if (ret != -1) {
    printf("  [FAIL] sigaction(0) expected -1, got %d\n", ret);
    exit(1);
  }
  ret = sigaction(NSIG, &act, 0);
  if (ret != -1) {
    printf("  [FAIL] sigaction(NSIG) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Invalid signal numbers rejected\n");

  // Test 3.4: Đăng ký user handler có sa_restorer
  memset(&act, 0, sizeof(act));
  act.sa_handler = custom_handler;
  act.sa_mask = 0;
  act.sa_flags = 0;
  act.sa_restorer = sigrestorer;

  ret = sigaction(SIGUSR1, &act, &oldact);
  if (ret < 0) {
    printf("  [FAIL] sigaction with custom_handler failed\n");
    exit(1);
  }
  if (oldact.sa_handler != SIG_IGN) {
    printf("  [FAIL] oldact was not SIG_IGN\n");
    exit(1);
  }
  printf("  [PASS] Registered custom_handler and retrieved oldact correctly\n");

  printf("  => sigaction tests: ALL PASSED\n\n");
}

void
test_kill(void)
{
  printf("=== 4. Test kill ===\n");

  int ret;

  // Test 4.1: Signal không hợp lệ
  ret = kill(getpid(), 0);
  if (ret != -1) {
    printf("  [FAIL] kill(self, 0) expected -1, got %d\n", ret);
    exit(1);
  }
  ret = kill(getpid(), NSIG);
  if (ret != -1) {
    printf("  [FAIL] kill(self, NSIG) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Invalid signal numbers rejected\n");

  // Test 4.2: PID không tồn tại
  ret = kill(99999, SIGUSR1);
  if (ret != -1) {
    printf("  [FAIL] kill(non-existent pid) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Non-existent PID rejected\n");

  printf("  => kill tests: ALL PASSED\n\n");
}

void
test_mask_pending_delivery(void)
{
  printf("=== 5. Test Mask -> Pending -> Unmask Delivery ===\n");

  handler_called = 0;

  // 1. Đăng ký handler cho SIGUSR1
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = custom_handler;
  act.sa_mask = 0;
  act.sa_flags = 0;
  act.sa_restorer = sigrestorer;
  int ret = sigaction(SIGUSR1, &act, 0);
  if (ret < 0) {
    printf("  [FAIL] sigaction failed\n");
    exit(1);
  }

  // 2. Chặn SIGUSR1
  unsigned int mask = (1U << SIGUSR1);
  sigprocmask(SIG_BLOCK, &mask, 0);

  // 3. Gửi SIGUSR1 cho chính mình
  printf("  -> Sending SIGUSR1 to self while BLOCKED...\n");
  kill(getpid(), SIGUSR1);

  // Handler KHÔNG được phép chạy vì đang bị mask
  if (handler_called != 0) {
    printf("  [FAIL] Handler executed while signal was blocked!\n");
    exit(1);
  }
  printf("  [PASS] Signal remained pending, handler not called yet\n");

  // 4. Bỏ chặn SIGUSR1 -> Kernel phải delivery ngay khi syscall sigprocmask hoàn tất
  printf("  -> Unblocking SIGUSR1...\n");
  sigprocmask(SIG_UNBLOCK, &mask, 0);

  // Handler PHẢI được gọi đúng 1 lần
  if (handler_called != 1) {
    printf("  [FAIL] Handler not executed after unblock! (count=%d)\n",
           handler_called);
    exit(1);
  }
  printf("  [PASS] Pending signal delivered immediately upon unblock!\n");

  printf("  => Mask & Delivery integration: ALL PASSED\n\n");
}
