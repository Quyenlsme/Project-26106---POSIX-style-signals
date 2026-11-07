// user/sigtest.c — Bo kiem thu ha tang Signal cho xv6-riscv
// Kiem tra tinh đung đan cua cac syscall: sigprocmask, alarm, sigaction, kill

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

// Forward declarations
void test_sigprocmask(void);
void test_alarm(void);
void test_sigaction(void);
void test_kill(void);
void test_mask_pending_delivery(void);

int
main(int argc, char *argv[])
{
  printf("\n========================================\n");
  printf("  SIGNAL SYSTEM INTEGRATION TEST \n");
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

  // Test 1.1: Chan SIGUSR1 qua SIG_BLOCK
  unsigned int block_set = (1U << SIGUSR1);
  ret = sigprocmask(SIG_BLOCK, &block_set, &oldmask);
  if (ret < 0) {
    printf("  [FAIL] SIG_BLOCK failed\n");
    exit(1);
  }
  printf("  [PASS] SIG_BLOCK SIGUSR1 (ret=%d, oldmask=0x%x)\n", ret, oldmask);

  // Test 1.2: Đoc mask hien tai (set = NULL)
  unsigned int cur_mask = 0;
  ret = sigprocmask(SIG_BLOCK, 0, &cur_mask);
  if (ret < 0 || (cur_mask & (1U << SIGUSR1)) == 0) {
    printf(
      "  [FAIL] Reading mask failed or SIGUSR1 not blocked (cur_mask=0x%x)\n",
      cur_mask);
    exit(1);
  }
  printf("  [PASS] Query current mask with set=NULL (mask=0x%x)\n", cur_mask);

  // Test 1.3: Chu dong chan SIGKILL (phai bi sig_sanitize loai bo)
  unsigned int kill_set = (1U << SIGKILL);
  ret = sigprocmask(SIG_BLOCK, &kill_set, 0);
  sigprocmask(SIG_BLOCK, 0, &cur_mask);
  if (cur_mask & (1U << SIGKILL)) {
    printf("  [FAIL] SIGKILL was blocked! Invariant violated!\n");
    exit(1);
  }
  printf("  [PASS] Invariant: SIGKILL cannot be blocked (mask=0x%x)\n",
         cur_mask);

  // Test 1.4: Bo chan SIGUSR1 qua SIG_UNBLOCK
  ret = sigprocmask(SIG_UNBLOCK, &block_set, &oldmask);
  sigprocmask(SIG_BLOCK, 0, &cur_mask);
  if (ret < 0 || (cur_mask & (1U << SIGUSR1)) != 0) {
    printf("  [FAIL] SIG_UNBLOCK failed\n");
    exit(1);
  }
  printf("  [PASS] SIG_UNBLOCK SIGUSR1 (ret=%d, cur_mask=0x%x)\n", ret,
         cur_mask);

  // Test 1.5: Tham so how khong hop le
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

  // Test 2.1: Đat alarm lan đau
  ret = alarm(10);
  if (ret != 0) {
    printf("  [FAIL] First alarm(10) expected 0, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] First alarm(10) returned 0\n");

  // Test 2.2: Thay the deadline cu, nhan lai so tick con lai
  ret = alarm(25);
  if (ret <= 0) {
    printf("  [FAIL] Replacing alarm expected remaining > 0, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Replaced alarm(25), remaining ticks returned: %d\n", ret);

  // Test 2.3: Huy alarm bang alarm(0)
  ret = alarm(0);
  if (ret <= 0) {
    printf("  [FAIL] alarm(0) expected remaining > 0, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Canceled alarm(0), remaining ticks returned: %d\n", ret);

  // Test 2.4: Kiem tra sau khi huy, alarm(5) tra ve '0'
  ret = alarm(5);
  if (ret != 0) {
    printf("  [FAIL] alarm(5) after cancel expected 0, got %d\n", ret);
    exit(1);
  }
  alarm(0); // Clear
  printf("  [PASS] Verified clean state after cancel\n");

  // Test 2.5: Tham so am khong hop le
  ret = alarm(-5);
  if (ret != -1) {
    printf("  [FAIL] alarm(-5) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Negative ticks rejected with -1\n");

  // Test 2.6: AL-07 - Fork khong thua ke alarm
  alarm(100);
  int fpid = fork();
  if (fpid < 0) {
    printf("  [FAIL] fork failed in alarm test\n");
    exit(1);
  }
  if (fpid == 0) {
    // Tien trinh con: kiem tra alarm(0) có trả ve '0' không
    // Neu con không thua ke (alarm_deadline == 0), alarm(0) phai tra ve 0
    int child_rem = alarm(0);
    if (child_rem != 0) {
      exit(1);
    }
    exit(0);
  }
  alarm(0); // Cha don dep timer
  int status = 0;
  wait(&status);
  if (status != 0) {
    printf("  [FAIL] AL-07: child process inherited alarm deadline!\n");
    exit(1);
  }
  printf("  [PASS] AL-07: child does not inherit alarm deadline after fork\n");

  printf("  => alarm tests: ALL PASSED\n\n");
}

void
test_sigaction(void)
{
  printf("=== 3. Test sigaction ===\n");

  int ret;
  struct sigaction act, oldact;

  // Test 3.1: SIG_IGN cho SIGUSR1
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

  // Test 3.2: Khong the can thiep SIGKILL
  ret = sigaction(SIGKILL, &act, 0);
  if (ret != -1) {
    printf("  [FAIL] sigaction(SIGKILL) expected -1, got %d\n", ret);
    exit(1);
  }
  printf("  [PASS] Invariant: SIGKILL cannot be modified\n");

  // Test 3.3: So hieu signal không hop le
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

  // Test 3.4: Dang ky user handler có sa_restorer
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

  // Test 4.1: Signal khong hop le
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

  // Test 4.2: PID khong ton tai
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

  // 1. Dang ky handler cho SIGUSR1
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

  // 2. Chan SIGUSR1
  unsigned int mask = (1U << SIGUSR1);
  sigprocmask(SIG_BLOCK, &mask, 0);

  // 3. Send SIGUSR1 cho chinh minh
  printf("  -> Sending SIGUSR1 to self while BLOCKED...\n");
  kill(getpid(), SIGUSR1);

  // Handler KHONG duoc phep chay vi dang bi mask
  if (handler_called != 0) {
    printf("  [FAIL] Handler executed while signal was blocked!\n");
    exit(1);
  }
  printf("  [PASS] Signal remained pending, handler not called yet\n");

  // 4. Bo chan SIGUSR1 -> Kernel phai delivery ngay khi syscall sigprocmask hoan tat
  printf("  -> Unblocking SIGUSR1...\n");
  sigprocmask(SIG_UNBLOCK, &mask, 0);

  // Handler PHAI duoc goi dung 1 lan
  if (handler_called != 1) {
    printf("  [FAIL] Handler not executed after unblock! (count=%d)\n",
           handler_called);
    exit(1);
  }
  printf("  [PASS] Pending signal delivered immediately upon unblock!\n");

  printf("  => Mask & Delivery integration: ALL PASSED\n\n");
}
