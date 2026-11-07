
#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

int handler_called = 0;
int fork_handler_called = 0;

void
fork_test_handler(int sig)
{
  if (sig == SIGUSR1)
    fork_handler_called++;
}

unsigned int handler_mask = 0;
// Bien va ham phuc vu REG-06
int nested_order[3];
int nested_count = 0;
int nested_error = 0;

void
nested_sigusr2_handler(int sig)
{
  if (sig == SIGUSR2) {
    nested_order[nested_count++] = 2;
  }
}

void
nested_sigusr1_handler(int sig)
{
  if (sig == SIGUSR1) {
    nested_order[nested_count++] = 1;

    // Gui SIGUSR2 ngay trong handler SIGUSR1
    if (kill(getpid(), SIGUSR2) < 0)
      nested_error = 1;

    // Danh dau handler SIGUSR2 da ket thuc va quay lai SIGUSR1
    nested_order[nested_count++] = 3;
  }
}

void
mask_handler(int sig)
{
  unsigned int current_mask = 0;

  if (sig == SIGUSR1) {
    handler_called++;
    sigprocmask(SIG_BLOCK, 0, &current_mask);
    handler_mask = current_mask;
  }
}

void
test_handler(int sig)
{
  if (sig == SIGUSR1 || sig == SIGUSR2)
    handler_called++;
}

int
main(int argc, char *argv[])
{
  struct sigaction act;
  // REG-28 child mode: verify signal state after exec
  if (argc == 2 && strcmp(argv[1], "exec-child") == 0) {
    struct sigaction usr1_action, usr2_action;
    sigset_t current_mask = 0;

    if (sigaction(SIGUSR1, 0, &usr1_action) < 0)
      exit(1);

    if (sigaction(SIGUSR2, 0, &usr2_action) < 0)
      exit(1);

    if (sigprocmask(SIG_BLOCK, 0, &current_mask) < 0)
      exit(1);

    if (usr1_action.sa_handler == SIG_DFL &&
        usr2_action.sa_handler == SIG_IGN && (current_mask & (1U << SIGUSR2))) {
      exit(0);
    }

    exit(1);
  }

  printf("\n===== SIGNAL REGISTER TEST =====\n");

  // REG-01: Kiem tra signal delivery
  handler_called = 0;

  act.sa_handler = test_handler;
  act.sa_mask = 0;
  act.sa_flags = 0;
  act.sa_restorer = sigrestorer;

  if (sigaction(SIGUSR1, &act, 0) < 0) {
    printf("REG-01 FAIL: sigaction\n");
    exit(1);
  }

  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-01 FAIL: kill\n");
    exit(1);
  }

  if (handler_called == 1) {
    printf("REG-01 PASS: Signal handler was called\n");
  } else {
    printf("REG-01 FAIL: handler_called = %d\n", handler_called);
    exit(1);
  }

  // REG-02: Kiem tra bao toan thanh ghi s1-s11
  handler_called = 0;

  register uint64 r1 asm("s1") = 0x111;
  register uint64 r2 asm("s2") = 0x222;
  register uint64 r3 asm("s3") = 0x333;
  register uint64 r4 asm("s4") = 0x444;
  register uint64 r5 asm("s5") = 0x555;
  register uint64 r6 asm("s6") = 0x666;
  register uint64 r7 asm("s7") = 0x777;
  register uint64 r8 asm("s8") = 0x888;
  register uint64 r9 asm("s9") = 0x999;
  register uint64 r10 asm("s10") = 0xaaa;
  register uint64 r11 asm("s11") = 0xbbb;

  // Dua cac gia tri vao cac thanh ghi can kiem tra
  asm volatile(""
               :
               : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r6), "r"(r7),
                 "r"(r8), "r"(r9), "r"(r10), "r"(r11)
               : "memory");

  // Gui signal de kernel thuc hien signal delivery
  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-02 FAIL: kill\n");
    exit(1);
  }

  // Doc lai gia tri cac thanh ghi sau khi handler ket thuc
  asm volatile(""
               : "=r"(r1), "=r"(r2), "=r"(r3), "=r"(r4), "=r"(r5), "=r"(r6),
                 "=r"(r7), "=r"(r8), "=r"(r9), "=r"(r10), "=r"(r11)
               :
               : "memory");

  if (handler_called != 1) {
    printf("REG-02 FAIL: handler_called = %d\n", handler_called);
    exit(1);
  }

  if (r1 != 0x111 || r2 != 0x222 || r3 != 0x333 || r4 != 0x444 || r5 != 0x555 ||
      r6 != 0x666 || r7 != 0x777 || r8 != 0x888 || r9 != 0x999 ||
      r10 != 0xaaa || r11 != 0xbbb) {
    printf("REG-02 FAIL: Register values were not preserved\n");
    printf("s1-s11: %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx %lx\n", r1, r2, r3,
           r4, r5, r6, r7, r8, r9, r10, r11);
    exit(1);
  }

  printf("REG-02 PASS: s1-s11 preserved after signal\n");

  // REG-03: Kiem tra khoi phuc stack pointer sau sigreturn
  handler_called = 0;

  uint64 sp_before;
  uint64 sp_after;

  // Luu stack pointer truoc khi gui signal
  asm volatile("mv %0, sp" : "=r"(sp_before));

  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-03 FAIL: kill\n");
    exit(1);
  }

  // Luu stack pointer sau khi signal handler ket thuc
  asm volatile("mv %0, sp" : "=r"(sp_after));

  if (handler_called != 1) {
    printf("REG-03 FAIL: handler was not called\n");
    exit(1);
  }

  if (sp_before != sp_after) {
    printf("REG-03 FAIL: stack pointer was not restored\n");
    printf("Before: %lx, After: %lx\n", sp_before, sp_after);
    exit(1);
  }

  printf("REG-03 PASS: stack pointer restored after sigreturn\n");

  // REG-04: Kiem tra signal bi block
  handler_called = 0;

  unsigned int mask = (1U << SIGUSR1);

  // Block SIGUSR1
  if (sigprocmask(SIG_BLOCK, &mask, 0) < 0) {
    printf("REG-04 FAIL: cannot block SIGUSR1\n");
    exit(1);
  }

  // Gui SIGUSR1 khi dang bi block
  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-04 FAIL: kill\n");
    exit(1);
  }

  // Handler khong duoc chay khi signal dang bi block
  if (handler_called != 0) {
    printf("REG-04 FAIL: handler ran while signal was blocked\n");
    exit(1);
  }

  // Unblock SIGUSR1
  if (sigprocmask(SIG_UNBLOCK, &mask, 0) < 0) {
    printf("REG-04 FAIL: cannot unblock SIGUSR1\n");
    exit(1);
  }

  // Signal pending phai duoc xu ly sau khi unblock
  if (handler_called != 1) {
    printf("REG-04 FAIL: pending signal was not delivered\n");
    exit(1);
  }

  printf("REG-04 PASS: blocked signal delivered after unblock\n");

  // REG-05: Kiem tra khoi phuc signal mask sau handler
  handler_called = 0;
  handler_mask = 0;

  unsigned int initial_mask = (1U << SIGUSR2);
  unsigned int current_mask = 0;

  // Block SIGUSR2 truoc khi gui SIGUSR1
  if (sigprocmask(SIG_SETMASK, &initial_mask, 0) < 0) {
    printf("REG-05 FAIL: cannot set initial mask\n");
    exit(1);
  }

  // Dang ky handler moi
  act.sa_handler = mask_handler;
  act.sa_mask = 0;
  act.sa_flags = 0;
  act.sa_restorer = sigrestorer;

  if (sigaction(SIGUSR1, &act, 0) < 0) {
    printf("REG-05 FAIL: sigaction\n");
    exit(1);
  }

  // Gui SIGUSR1
  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-05 FAIL: kill\n");
    exit(1);
  }

  if (handler_called != 1) {
    printf("REG-05 FAIL: handler was not called\n");
    exit(1);
  }

  // Kiem tra mask trong handler
  if (!(handler_mask & (1U << SIGUSR1)) || !(handler_mask & (1U << SIGUSR2))) {
    printf("REG-05 FAIL: handler mask is incorrect\n");
    printf("Handler mask: %x\n", handler_mask);
    exit(1);
  }

  // Doc mask sau khi handler ket thuc
  if (sigprocmask(SIG_BLOCK, 0, &current_mask) < 0) {
    printf("REG-05 FAIL: cannot read current mask\n");
    exit(1);
  }

  // SIGUSR2 phai van bi block, SIGUSR1 phai duoc unblock
  if (!(current_mask & (1U << SIGUSR2)) || (current_mask & (1U << SIGUSR1))) {
    printf("REG-05 FAIL: signal mask was not restored\n");
    printf("Current mask: %x\n", current_mask);
    exit(1);
  }

  printf("REG-05 PASS: signal mask restored after handler\n");

  // Don dep: unblock SIGUSR2
  unsigned int cleanup_mask = (1U << SIGUSR2);
  sigprocmask(SIG_UNBLOCK, &cleanup_mask, 0);

  // REG-06: Kiem tra nested signal handler
  nested_count = 0;
  nested_error = 0;

  struct sigaction act_usr1;
  struct sigaction act_usr2;

  act_usr1.sa_handler = nested_sigusr1_handler;
  act_usr1.sa_mask = 0;
  act_usr1.sa_flags = 0;
  act_usr1.sa_restorer = sigrestorer;

  act_usr2.sa_handler = nested_sigusr2_handler;
  act_usr2.sa_mask = 0;
  act_usr2.sa_flags = 0;
  act_usr2.sa_restorer = sigrestorer;

  if (sigaction(SIGUSR1, &act_usr1, 0) < 0 ||
      sigaction(SIGUSR2, &act_usr2, 0) < 0) {
    printf("REG-06 FAIL: sigaction\n");
    exit(1);
  }

  // Gui SIGUSR1 de bat dau chuoi nested signal
  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-06 FAIL: kill SIGUSR1\n");
    exit(1);
  }

  if (nested_error) {
    printf("REG-06 FAIL: kill SIGUSR2 inside handler\n");
    exit(1);
  }

  if (nested_count != 3 || nested_order[0] != 1 || nested_order[1] != 2 ||
      nested_order[2] != 3) {
    printf("REG-06 FAIL: incorrect nested signal order\n");
    printf("Order: %d %d %d, count = %d\n", nested_order[0], nested_order[1],
           nested_order[2], nested_count);
    exit(1);
  }

  printf("REG-06 PASS: nested signal handler executed correctly\n");

  // REG-07: Kiem tra SIG_IGN
  handler_called = 0;

  act.sa_handler = SIG_IGN;
  act.sa_mask = 0;
  act.sa_flags = 0;
  act.sa_restorer = sigrestorer;

  if (sigaction(SIGUSR1, &act, 0) < 0) {
    printf("REG-07 FAIL: sigaction\n");
    exit(1);
  }

  // Gui SIGUSR1 khi signal duoc dat la SIG_IGN
  if (kill(getpid(), SIGUSR1) < 0) {
    printf("REG-07 FAIL: kill\n");
    exit(1);
  }

  // Handler cu khong duoc goi
  if (handler_called != 0) {
    printf("REG-07 FAIL: ignored signal still invoked handler\n");
    exit(1);
  }

  printf("REG-07 PASS: SIG_IGN ignored the signal\n");

  // REG-08: Kiem tra SIG_DFL
  int pid;
  int status = 0;

  pid = fork();

  if (pid < 0) {
    printf("REG-08 FAIL: fork\n");
    exit(1);
  }

  if (pid == 0) {
    struct sigaction default_act;

    default_act.sa_handler = SIG_DFL;
    default_act.sa_mask = 0;
    default_act.sa_flags = 0;
    default_act.sa_restorer = sigrestorer;

    if (sigaction(SIGUSR1, &default_act, 0) < 0)
      exit(2);

    // Gui signal co hanh vi mac dinh ket thuc tien trinh
    kill(getpid(), SIGUSR1);

    // Neu van chay den day thi SIG_DFL khong hoat dong dung
    exit(3);
  }

  if (wait(&status) != pid) {
    printf("REG-08 FAIL: wait\n");
    exit(1);
  }

  if (status == -1) {
    printf("REG-08 PASS: SIG_DFL terminated child process\n");
  } else {
    printf("REG-08 FAIL: child exit status = %d\n", status);
    exit(1);
  }

  // REG-09: Kiem tra khong the block SIGKILL
  unsigned int kill_mask = (1U << SIGKILL);
  unsigned int check_mask = 0;

  // Co gang block SIGKILL
  if (sigprocmask(SIG_BLOCK, &kill_mask, 0) < 0) {
    printf("REG-09 FAIL: sigprocmask\n");
    exit(1);
  }

  // Doc lai signal mask
  if (sigprocmask(SIG_BLOCK, 0, &check_mask) < 0) {
    printf("REG-09 FAIL: cannot read signal mask\n");
    exit(1);
  }

  // SIGKILL khong duoc xuat hien trong blocked mask
  if (check_mask & (1U << SIGKILL)) {
    printf("REG-09 FAIL: SIGKILL was blocked\n");
    printf("Current mask: %x\n", check_mask);
    exit(1);
  }

  printf("REG-09 PASS: SIGKILL cannot be blocked\n");

  // REG-10: Kiem tra sigprocmask voi NULL
  unsigned int mask_before = 0;
  unsigned int mask_after = 0;

  // Tao mask ban dau chi block SIGUSR2
  unsigned int test_mask = (1U << SIGUSR2);

  if (sigprocmask(SIG_SETMASK, &test_mask, 0) < 0) {
    printf("REG-10 FAIL: cannot set initial mask\n");
    exit(1);
  }

  // Chi doc mask, khong thay doi mask
  if (sigprocmask(SIG_BLOCK, 0, &mask_before) < 0) {
    printf("REG-10 FAIL: cannot read mask\n");
    exit(1);
  }

  // Goi syscall voi ca hai con tro NULL
  if (sigprocmask(SIG_BLOCK, 0, 0) < 0) {
    printf("REG-10 FAIL: NULL arguments\n");
    exit(1);
  }

  // Doc lai mask
  if (sigprocmask(SIG_BLOCK, 0, &mask_after) < 0) {
    printf("REG-10 FAIL: cannot read mask again\n");
    exit(1);
  }

  if (mask_before != test_mask || mask_after != mask_before) {
    printf("REG-10 FAIL: signal mask changed unexpectedly\n");
    printf("Before: %x, After: %x\n", mask_before, mask_after);
    exit(1);
  }

  printf("REG-10 PASS: sigprocmask handles NULL arguments\n");

  // Don dep
  sigprocmask(SIG_UNBLOCK, &test_mask, 0);

  // REG-11: Test sigaction filters unsupported signals in sa_mask
  {
    struct sigaction act, oldact, checkact;

    act.sa_handler = SIG_IGN;
    act.sa_mask = (1U << SIGUSR2) | (1U << 20) | (1U << SIGKILL);
    act.sa_flags = 0;
    act.sa_restorer = 0;

    // Save old action and register the new action
    if (sigaction(SIGUSR1, &act, &oldact) < 0) {
      printf("REG-11 FAIL: sigaction registration failed\n");
    } else if (sigaction(SIGUSR1, 0, &checkact) < 0) {
      printf("REG-11 FAIL: cannot read back sigaction\n");
    } else if ((checkact.sa_mask & (1U << SIGUSR2)) == 0 ||
               (checkact.sa_mask & (1U << 20)) != 0 ||
               (checkact.sa_mask & (1U << SIGKILL)) != 0) {
      printf("REG-11 FAIL: sa_mask was not sanitized correctly\n");
    } else {
      printf("REG-11 PASS: sigaction filters unsupported signals\n");
    }

    // Restore the original action
    sigaction(SIGUSR1, &oldact, 0);
  }

  // REG-12: Test sigaction rejects invalid signals
  {
    int pass = 1;

    if (sigaction(0, 0, 0) != -1)
      pass = 0;

    if (sigaction(-1, 0, 0) != -1)
      pass = 0;

    if (sigaction(NSIG, 0, 0) != -1)
      pass = 0;

    if (sigaction(SIGKILL, 0, 0) != -1)
      pass = 0;

    if (pass)
      printf("REG-12 PASS: sigaction rejects invalid signals\n");
    else
      printf("REG-12 FAIL: sigaction accepted an invalid signal\n");
  }

  // REG-13: Test sigaction rejects unsupported flags
  {
    struct sigaction act;

    act.sa_handler = SIG_IGN;
    act.sa_mask = 0;
    act.sa_flags = 1;
    act.sa_restorer = 0;

    if (sigaction(SIGUSR1, &act, 0) == -1) {
      printf("REG-13 PASS: sigaction rejects unsupported flags\n");
    } else {
      printf("REG-13 FAIL: sigaction accepted unsupported flags\n");
    }
  }

  // REG-14: Test sigaction requires sa_restorer
  {
    struct sigaction act;

    act.sa_handler = test_handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    act.sa_restorer = 0;

    if (sigaction(SIGUSR1, &act, 0) == -1) {
      printf("REG-14 PASS: sigaction rejects handler without restorer\n");
    } else {
      printf("REG-14 FAIL: sigaction accepted handler without restorer\n");
    }
  }

  // REG-15: Test sigaction returns old action
  {
    struct sigaction act1, act2, oldact;

    act1.sa_handler = SIG_IGN;
    act1.sa_mask = 0;
    act1.sa_flags = 0;
    act1.sa_restorer = 0;

    act2.sa_handler = SIG_DFL;
    act2.sa_mask = 0;
    act2.sa_flags = 0;
    act2.sa_restorer = 0;

    // Install the first action
    if (sigaction(SIGUSR1, &act1, 0) < 0) {
      printf("REG-15 FAIL: cannot install first action\n");
    } else if (sigaction(SIGUSR1, &act2, &oldact) < 0) {
      printf("REG-15 FAIL: cannot replace action\n");
    } else if (oldact.sa_handler != SIG_IGN) {
      printf("REG-15 FAIL: old action was not returned\n");
    } else {
      printf("REG-15 PASS: sigaction returns old action\n");
    }

    // Restore default action
    sigaction(SIGUSR1, &act1, 0);
  }

  // REG-16: Test sigaction rejects invalid user address
  {
    if (sigaction(SIGUSR1, (struct sigaction *)-1, 0) == -1) {
      printf("REG-16 PASS: sigaction rejects invalid act address\n");
    } else {
      printf("REG-16 FAIL: sigaction accepted invalid act address\n");
    }
  }

  // REG-17: Test sigaction rejects invalid oldact address
  {
    if (sigaction(SIGUSR1, 0, (struct sigaction *)-1) == -1) {
      printf("REG-17 PASS: sigaction rejects invalid oldact address\n");
    } else {
      printf("REG-17 FAIL: sigaction accepted invalid oldact address\n");
    }
  }

  // REG-18: Test sigprocmask rejects invalid how
  {
    sigset_t set = (1U << SIGUSR2);

    // Set != NULL, invalid how must return -1
    if (sigprocmask(99, &set, 0) != -1) {
      printf("REG-18 FAIL: invalid how was accepted\n");
    } else {
      printf("REG-18 PASS: sigprocmask rejects invalid how\n");
    }
  }

  // REG-19: Test invalid how when set is NULL
  {
    sigset_t before, after;

    sigprocmask(SIG_BLOCK, 0, &before);

    int result = sigprocmask(99, 0, &after);

    if (result == 0 && before == after) {
      printf("REG-19 PASS: NULL set does not change signal mask\n");
    } else {
      printf("REG-19 FAIL: unexpected result with NULL set\n");
    }
  }
  // REG-20: SIGKILL cannot be blocked using SIG_SETMASK
  {
    sigset_t set, oldset, newset;

    set = (1U << SIGKILL) | (1U << SIGUSR1);

    int result = sigprocmask(SIG_SETMASK, &set, &oldset);

    if (result == 0 && sigprocmask(SIG_BLOCK, 0, &newset) == 0 &&
        !(newset & (1U << SIGKILL)) && (newset & (1U << SIGUSR1))) {
      printf("REG-20 PASS: SIGKILL cannot be blocked using SIG_SETMASK\n");
    } else {
      printf("REG-20 FAIL: SIG_SETMASK did not sanitize SIGKILL\n");
    }

    // Restore original mask
    sigprocmask(SIG_SETMASK, &oldset, 0);
  }

  // REG-21: Test SIG_UNBLOCK
  {
    sigset_t set, oldset, newset;

    set = (1U << SIGUSR1) | (1U << SIGUSR2);

    // Block both signals
    sigprocmask(SIG_BLOCK, &set, &oldset);

    // Unblock only SIGUSR1
    sigset_t unblock_set = (1U << SIGUSR1);
    int result = sigprocmask(SIG_UNBLOCK, &unblock_set, 0);

    // Read current mask
    sigprocmask(SIG_BLOCK, 0, &newset);

    if (result == 0 && !(newset & (1U << SIGUSR1)) &&
        (newset & (1U << SIGUSR2))) {
      printf("REG-21 PASS: SIG_UNBLOCK only unblocks requested signal\n");
    } else {
      printf("REG-21 FAIL: SIG_UNBLOCK modified unexpected signals\n");
    }

    // Restore original mask
    sigprocmask(SIG_SETMASK, &oldset, 0);
  }

  // REG-22: Test sigaction with both act and oldact
  {
    struct sigaction old_action, new_action, returned_action;

    // Set initial action to SIG_IGN
    new_action.sa_handler = SIG_IGN;
    new_action.sa_mask = 0;
    new_action.sa_flags = 0;
    new_action.sa_restorer = 0;

    sigaction(SIGUSR1, &new_action, 0);

    // Set new action to SIG_DFL and retrieve old action
    new_action.sa_handler = SIG_DFL;

    int result = sigaction(SIGUSR1, &new_action, &old_action);

    // Read current action
    sigaction(SIGUSR1, 0, &returned_action);

    if (result == 0 && old_action.sa_handler == SIG_IGN &&
        returned_action.sa_handler == SIG_DFL) {
      printf("REG-22 PASS: sigaction updates action and returns old action\n");
    } else {
      printf("REG-22 FAIL: sigaction did not handle both pointers correctly\n");
    }

    // Restore original action
    sigaction(SIGUSR1, &old_action, 0);
  }

  // REG-23: SIG_IGN does not require sa_restorer
  {
    struct sigaction old_action, new_action, current_action;

    sigaction(SIGUSR1, 0, &old_action);

    new_action.sa_handler = SIG_IGN;
    new_action.sa_mask = 0;
    new_action.sa_flags = 0;
    new_action.sa_restorer = 0;

    int result = sigaction(SIGUSR1, &new_action, 0);

    sigaction(SIGUSR1, 0, &current_action);

    if (result == 0 && current_action.sa_handler == SIG_IGN &&
        current_action.sa_restorer == 0) {
      printf("REG-23 PASS: SIG_IGN does not require sa_restorer\n");
    } else {
      printf("REG-23 FAIL: SIG_IGN incorrectly requires sa_restorer\n");
    }

    sigaction(SIGUSR1, &old_action, 0);
  }

  // REG-24: Verify multiple pending signals are delivered
  {
    sigset_t set, oldset, newset;
    struct sigaction old_usr1, old_usr2, action;

    int before = handler_called;

    sigaction(SIGUSR1, 0, &old_usr1);
    sigaction(SIGUSR2, 0, &old_usr2);

    action.sa_handler = test_handler;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = sigrestorer;

    sigaction(SIGUSR1, &action, 0);
    sigaction(SIGUSR2, &action, 0);

    set = (1U << SIGUSR1) | (1U << SIGUSR2);

    sigprocmask(SIG_BLOCK, &set, &oldset);

    kill(getpid(), SIGUSR1);
    kill(getpid(), SIGUSR2);

    sigprocmask(SIG_BLOCK, 0, &newset);

    printf("Before unblock: handler_called=%d\n", handler_called);
    printf("Blocked mask before unblock: %d\n", (int)newset);

    sigprocmask(SIG_UNBLOCK, &set, 0);

    printf("After unblock: handler_called=%d\n", handler_called);

    getpid();
    printf("After getpid: handler_called=%d\n", handler_called);

    printf("Expected handler_called=%d\n", before + 2);

    if (handler_called == before + 2) {
      printf("REG-24 PASS: Multiple pending signals were delivered\n");
    } else {
      printf("REG-24 FAIL: Not all pending signals were delivered\n");
    }

    sigaction(SIGUSR1, &old_usr1, 0);
    sigaction(SIGUSR2, &old_usr2, 0);
    sigprocmask(SIG_SETMASK, &oldset, 0);
  }

  // REG-25: Verify signal handler is inherited after fork
  {
    struct sigaction old_action, action;
    int status;
    int pid, waited;

    sigaction(SIGUSR1, 0, &old_action);

    action.sa_handler = fork_test_handler;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = sigrestorer;

    fork_handler_called = 0;

    if (sigaction(SIGUSR1, &action, 0) < 0) {
      printf("REG-25 FAIL: sigaction\n");
      exit(1);
    }

    pid = fork();

    if (pid < 0) {
      printf("REG-25 FAIL: fork\n");
      sigaction(SIGUSR1, &old_action, 0);
      exit(1);
    }

    if (pid == 0) {
      fork_handler_called = 0;

      if (kill(getpid(), SIGUSR1) < 0)
        exit(1);

      if (fork_handler_called == 1)
        exit(0);

      exit(1);
    }

    waited = wait(&status);

    sigaction(SIGUSR1, &old_action, 0);

    if (waited == pid && status == 0) {
      printf("REG-25 PASS: Child inherited signal handler after fork\n");
    } else {
      printf("REG-25 FAIL: Child did not inherit signal handler\n");
      exit(1);
    }
  }

  // REG-26: Child signal action changes must not affect parent
  {
    struct sigaction old_action, action, current_action;
    int pid, waited, status;

    sigaction(SIGUSR1, 0, &old_action);

    action.sa_handler = fork_test_handler;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = sigrestorer;

    if (sigaction(SIGUSR1, &action, 0) < 0) {
      printf("REG-26 FAIL: parent sigaction\n");
      exit(1);
    }

    pid = fork();

    if (pid < 0) {
      printf("REG-26 FAIL: fork\n");
      sigaction(SIGUSR1, &old_action, 0);
      exit(1);
    }

    if (pid == 0) {
      action.sa_handler = SIG_IGN;
      action.sa_mask = 0;
      action.sa_flags = 0;
      action.sa_restorer = 0;

      if (sigaction(SIGUSR1, &action, 0) < 0)
        exit(1);

      sigaction(SIGUSR1, 0, &current_action);

      if (current_action.sa_handler == SIG_IGN)
        exit(0);

      exit(1);
    }

    waited = wait(&status);

    sigaction(SIGUSR1, 0, &current_action);

    sigaction(SIGUSR1, &old_action, 0);

    if (waited == pid && status == 0 &&
        current_action.sa_handler == fork_test_handler) {
      printf("REG-26 PASS: Child signal action change did not affect parent\n");
    } else {
      printf("REG-26 FAIL: Parent signal action was modified\n");
      exit(1);
    }
  }

  // REG-27: Pending signals must not be inherited after fork
  {
    struct sigaction old_action, action;
    sigset_t set, oldset;
    int before = handler_called;
    int pid, waited, status;

    sigaction(SIGUSR1, 0, &old_action);

    action.sa_handler = test_handler;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = sigrestorer;

    if (sigaction(SIGUSR1, &action, 0) < 0) {
      printf("REG-27 FAIL: sigaction\n");
      exit(1);
    }

    set = (1U << SIGUSR1);

    if (sigprocmask(SIG_BLOCK, &set, &oldset) < 0) {
      printf("REG-27 FAIL: block signal\n");
      sigaction(SIGUSR1, &old_action, 0);
      exit(1);
    }

    // Queue SIGUSR1 in parent while it is blocked.
    if (kill(getpid(), SIGUSR1) < 0) {
      printf("REG-27 FAIL: kill\n");
      sigprocmask(SIG_SETMASK, &oldset, 0);
      sigaction(SIGUSR1, &old_action, 0);
      exit(1);
    }

    pid = fork();

    if (pid < 0) {
      printf("REG-27 FAIL: fork\n");
      sigprocmask(SIG_SETMASK, &oldset, 0);
      sigaction(SIGUSR1, &old_action, 0);
      exit(1);
    }

    if (pid == 0) {
      // Child must not receive the parent's pending SIGUSR1.
      if (sigprocmask(SIG_UNBLOCK, &set, 0) < 0)
        exit(1);

      getpid();

      if (handler_called == before)
        exit(0);

      exit(1);
    }

    waited = wait(&status);

    // Parent must still have its own pending SIGUSR1.
    if (sigprocmask(SIG_UNBLOCK, &set, 0) < 0) {
      printf("REG-27 FAIL: parent unblock\n");
      sigaction(SIGUSR1, &old_action, 0);
      exit(1);
    }

    getpid();

    sigaction(SIGUSR1, &old_action, 0);
    sigprocmask(SIG_SETMASK, &oldset, 0);

    if (waited == pid && status == 0 && handler_called == before + 1) {
      printf("REG-27 PASS: Pending signal was not inherited after fork\n");
    } else {
      printf("REG-27 FAIL: Pending signal inheritance is incorrect\n");
      exit(1);
    }
  }

  // REG-28: Verify signal disposition and mask after exec
  {
    struct sigaction old_usr1, old_usr2, action;
    sigset_t set, oldset;
    int pid, waited, status;

    sigaction(SIGUSR1, 0, &old_usr1);
    sigaction(SIGUSR2, 0, &old_usr2);

    // Register a caught handler for SIGUSR1.
    action.sa_handler = fork_test_handler;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = sigrestorer;

    if (sigaction(SIGUSR1, &action, 0) < 0) {
      printf("REG-28 FAIL: set SIGUSR1 handler\n");
      exit(1);
    }

    // Set SIGUSR2 to SIG_IGN.
    action.sa_handler = SIG_IGN;
    action.sa_mask = 0;
    action.sa_flags = 0;
    action.sa_restorer = 0;

    if (sigaction(SIGUSR2, &action, 0) < 0) {
      printf("REG-28 FAIL: set SIGUSR2 ignore\n");
      sigaction(SIGUSR1, &old_usr1, 0);
      exit(1);
    }

    // Block SIGUSR2 to check whether exec preserves the mask.
    set = (1U << SIGUSR2);

    if (sigprocmask(SIG_BLOCK, &set, &oldset) < 0) {
      printf("REG-28 FAIL: block SIGUSR2\n");
      sigaction(SIGUSR1, &old_usr1, 0);
      sigaction(SIGUSR2, &old_usr2, 0);
      exit(1);
    }

    pid = fork();

    if (pid < 0) {
      printf("REG-28 FAIL: fork\n");
      sigprocmask(SIG_SETMASK, &oldset, 0);
      sigaction(SIGUSR1, &old_usr1, 0);
      sigaction(SIGUSR2, &old_usr2, 0);
      exit(1);
    }

    if (pid == 0) {
      char *args[] = {"regtest", "exec-child", 0};

      exec("/regtest", args);

      // Reaching here means exec failed.
      exit(1);
    }

    waited = wait(&status);

    sigprocmask(SIG_SETMASK, &oldset, 0);
    sigaction(SIGUSR1, &old_usr1, 0);
    sigaction(SIGUSR2, &old_usr2, 0);

    if (waited == pid && status == 0) {
      printf(
        "REG-28 PASS: Signal actions and mask handled correctly after exec\n");
    } else {
      printf("REG-28 FAIL: Signal state after exec is incorrect\n");
      exit(1);
    }
  }

  printf("===== TEST COMPLETED =====\n");
  exit(0);
}
