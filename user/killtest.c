// user/killtest.c — Unit test cho module signal core (kill / sig_send)
// Dự án 26106 — POSIX-style signals trên xv6-riscv
// Người viết: Phạm Lê Quyền — Tuần 2
//
// Tuần 3 bổ sung (Test 7 - 12, đánh số theo week-03-plan):
//   - T7  DL-04: gộp signal - gửi 5 lần khi bị chặn, mở chặn handler chạy 1 lần
//   - T8  DL-05: signal số hiệu nhỏ hơn được giao trước
//   - T9  DL-09: SIG_IGN không kết thúc tiến trình, không ngắt read() đang chờ
//   - T10 DL-10: signal có handler ngắt read() đang chờ; 500 vòng không mất
//                wakeup (kiểm chứng p->chan = 0 và sleep_prepare_intr)
//   - T11: stress đa CPU - nhiều tiến trình đồng thời kill() một nạn nhân
//   - T12 DL-12: signal đang bị chặn không ngắt read() đang chờ
//
// Phạm vi Tuần 2 (Test 1 - 6):
//   - kill() kiểm tra tham số (sig, pid) -> -1 khi sai
//   - SIGKILL kết thúc tiến trình con đang chạy / đang ngủ, exit status = -1
//   - Signal thường với SIG_DFL -> hành động mặc định kết thúc tiến trình (-1)
//   - sig_send đánh thức tiến trình đang ngủ khi nhận signal thường

#include "kernel/signal.h"
#include "kernel/stat.h"
#include "kernel/types.h"
#include "user/user.h"

static int passed = 0;
static int failed = 0;

static volatile int usr1_count = 0;
static volatile int usr2_count = 0;
static volatile int order[4];
static volatile int norder = 0;

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

static void
on_usr1(int sig)
{
  usr1_count++;
}

static void
on_usr2(int sig)
{
  usr2_count++;
}

static void
on_record(int sig)
{
  if (norder < 4)
    order[norder++] = sig;
}

// Đăng ký handler (hoặc SIG_IGN / SIG_DFL) cho sig. Trả 0 nếu thành công.
static int
set_handler(int sig, void (*h)(int))
{
  struct sigaction act;
  memset(&act, 0, sizeof(act));
  act.sa_handler = h;
  act.sa_mask = 0;
  act.sa_flags = 0;
  act.sa_restorer = sigrestorer;
  return sigaction(sig, &act, 0);
}

static void
block_sig(int sig)
{
  unsigned int set = (1U << sig);
  sigprocmask(SIG_BLOCK, &set, 0);
}

static void
unblock_sig(int sig)
{
  unsigned int set = (1U << sig);
  sigprocmask(SIG_UNBLOCK, &set, 0);
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

// Test 4: SIGKILL đánh thức và kết thúc tiến trình con đang ngủ (block trên
// read pipe)
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

// Test 5: signal thường (SIGUSR1) với SIG_DFL -> hành động mặc định kết thúc
// tiến trình
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

// Test 6: signal thường đánh thức tiến trình đang ngủ (kiểm tra wakeup trong
// sig_send)
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

// Test 8 (DL-05): thứ tự ưu tiên - signal có số hiệu nhỏ hơn được giao trước
static void
test_signal_priority(void)
{
  printf("Test 8 (DL-05): signal so hieu nho duoc giao truoc\n");
  norder = 0;
  set_handler(SIGUSR1, on_record);
  set_handler(SIGUSR2, on_record);
  unsigned int both = (1U << SIGUSR1) | (1U << SIGUSR2);
  unsigned int none = 0;
  sigprocmask(SIG_BLOCK, &both, 0);
  kill(getpid(), SIGUSR2);            // gửi 12 trước
  kill(getpid(), SIGUSR1);            // rồi 10
  sigprocmask(SIG_SETMASK, &none, 0); // mở chặn cả hai cùng lúc
  check(norder == 2, "ca hai handler deu chay");
  check(order[0] == SIGUSR1, "SIGUSR1 (10) duoc giao truoc SIGUSR2 (12)");
  set_handler(SIGUSR1, SIG_DFL);
  set_handler(SIGUSR2, SIG_DFL);
}

// Test 7 (DL-04): gộp signal lặp lại khi đang bị chặn -> handler chạy đúng 1
// lần
static void
test_signal_coalescing(void)
{
  printf("Test 7 (DL-04): gop signal lap lai khi dang bi chan\n");
  usr1_count = 0;
  check(set_handler(SIGUSR1, on_usr1) == 0, "sigaction(SIGUSR1, handler) == 0");
  block_sig(SIGUSR1);
  int ok = 1;
  for (int i = 0; i < 5; i++)
    if (kill(getpid(), SIGUSR1) != 0)
      ok = 0;
  check(ok, "gui 5 lan SIGUSR1 cho chinh minh deu tra 0");
  check(usr1_count == 0, "handler chua chay khi dang bi chan");
  unblock_sig(SIGUSR1);
  check(usr1_count == 1, "mo chan -> handler chay dung 1 lan");

  // Biến thể: 5 lần từ một tiến trình khác
  usr1_count = 0;
  block_sig(SIGUSR1);
  int parent = getpid();
  int pid = fork();
  if (pid == 0) {
    for (int i = 0; i < 5; i++)
      kill(parent, SIGUSR1);
    exit(0);
  }
  wait(0);
  check(usr1_count == 0, "5 SIGUSR1 tu tien trinh khac: handler chua chay");
  unblock_sig(SIGUSR1);
  check(usr1_count == 1, "mo chan -> handler chay dung 1 lan");
  set_handler(SIGUSR1, SIG_DFL);
}

// Test 10 (DL-10): signal có handler ngắt read() đang chờ; read trả -1,
// handler chạy, tiến trình tiếp tục sống (không bị kết thúc)
static void
test_interrupt_with_handler(void)
{
  printf("Test 10 (DL-10): signal co handler ngat read() dang cho\n");
  int ready[2], block[2];
  if (pipe(ready) < 0 || pipe(block) < 0) {
    check(0, "pipe");
    return;
  }
  int pid = fork();
  if (pid == 0) {
    char c;
    close(ready[0]);
    close(block[1]);
    usr1_count = 0;
    set_handler(SIGUSR1, on_usr1);
    write(ready[1], "r", 1);
    int r = 0;
    if (usr1_count == 0)
      r = read(block[0], &c, 1); // ngủ tới khi bị signal ngắt
    else
      r = -1;
    exit((r == -1 && usr1_count == 1) ? 5 : 1);
  }
  close(ready[1]);
  close(block[0]);
  char c;
  read(ready[0], &c, 1);
  spin(1000000);
  check(kill(pid, SIGUSR1) == 0, "kill(child dang ngu, SIGUSR1) == 0");
  int status = 0;
  wait(&status);
  check(status == 5, "read() tra -1, handler chay 1 lan, con song va exit(5)");
  close(ready[0]);
  close(block[1]);
}

// Test 10b (DL-10): lặp 500 vòng "con vừa vào read() thì cha gửi signal".
// Nếu signal rơi vào khe giữa lúc kiểm tra và lúc ngủ mà bị mất, con chỉ
// thức dậy nhờ alarm dự phòng -> đếm là 1 lần mất wakeup.
#define NLOOP 500
static volatile int alrm_count = 0;

static void
on_alrm(int sig)
{
  alrm_count++;
}

static void
test_no_lost_wakeup(void)
{
  int ready[2], block[2];
  if (pipe(ready) < 0 || pipe(block) < 0) {
    check(0, "pipe");
    return;
  }
  int pid = fork();
  if (pid == 0) {
    char c;
    close(ready[0]);
    close(block[1]);
    usr1_count = 0;
    alrm_count = 0;
    set_handler(SIGUSR1, on_usr1);
    set_handler(SIGALRM, on_alrm);
    int lost = 0;
    for (int i = 0; i < NLOOP; i++) {
      int before = usr1_count;
      alarm(3);
      write(ready[1], "r", 1);
      if (usr1_count == before)
        read(block[0], &c, 1);
      alarm(0);
      while (usr1_count == before)
        ;
      if (alrm_count) {
        lost++;
        alrm_count = 0;
      }
    }
    exit(lost > 100 ? 100 : lost);
  }
  close(ready[1]);
  close(block[0]);
  char c;
  for (int i = 0; i < NLOOP; i++) {
    read(ready[0], &c, 1);
    kill(pid, SIGUSR1);
  }
  int status = -1;
  wait(&status);
  check(status == 0, "500 vong gui signal luc con vua vao read(): 0 lan mat "
                     "wakeup");
  close(ready[0]);
  close(block[1]);
}

// Test 9 + 12: signal bị SIG_IGN / bị chặn KHÔNG được ngắt read() đang chờ
static void
test_no_interrupt(int sig, int use_block, int code, char *name)
{
  int ready[2], data[2];
  if (pipe(ready) < 0 || pipe(data) < 0) {
    check(0, "pipe");
    return;
  }
  int pid = fork();
  if (pid == 0) {
    char c;
    close(ready[0]);
    close(data[1]);
    if (use_block)
      block_sig(sig);
    else
      set_handler(sig, SIG_IGN);
    write(ready[1], "r", 1);
    int r = read(data[0], &c, 1);
    exit((r == 1 && c == 'x') ? code : 1);
  }
  close(ready[1]);
  close(data[0]);
  char c;
  read(ready[0], &c, 1);
  spin(1000000);
  kill(pid, sig);
  spin(1000000);
  write(data[1], "x", 1); // con phải vẫn đang chờ và nhận được dữ liệu
  int status = 0;
  wait(&status);
  check(status == code, name);
  close(ready[0]);
  close(data[1]);
}

// Test 11: stress đa CPU - nhiều tiến trình đồng thời kill() một nạn nhân
#define NSENDER 4
#define NSEND   200
static void
test_multicpu_stress(void)
{
  printf("Test 11: stress da CPU (%d tien trinh x %d signal)\n", NSENDER,
         NSEND);

  // 11a: nạn nhân chặn SIGUSR1/SIGUSR2, các sender bắn đồng thời,
  // mở chặn -> mỗi signal chạy handler đúng 1 lần (không mất bit, có gộp)
  int done[2], res[2];
  if (pipe(done) < 0 || pipe(res) < 0) {
    check(0, "pipe");
    return;
  }
  int victim = fork();
  if (victim == 0) {
    char c;
    close(done[1]);
    close(res[0]);
    usr1_count = usr2_count = 0;
    set_handler(SIGUSR1, on_usr1);
    set_handler(SIGUSR2, on_usr2);
    block_sig(SIGUSR1);
    block_sig(SIGUSR2);
    write(res[1], "r", 1);
    while (read(done[0], &c, 1) != 1)
      ;
    int before = usr1_count + usr2_count;
    unblock_sig(SIGUSR1);
    unblock_sig(SIGUSR2);
    char out[3] = {before, usr1_count, usr2_count};
    write(res[1], out, 3);
    exit(0);
  }
  close(done[0]);
  close(res[1]);
  char c;
  read(res[0], &c, 1);

  int okall = 1;
  for (int s = 0; s < NSENDER; s++) {
    if (fork() == 0) {
      for (int i = 0; i < NSEND; i++)
        if (kill(victim, (i & 1) ? SIGUSR2 : SIGUSR1) != 0)
          exit(1);
      exit(0);
    }
  }
  for (int s = 0; s < NSENDER; s++) {
    int st = 0;
    wait(&st);
    if (st != 0)
      okall = 0;
  }
  check(okall, "moi lenh kill() dong thoi deu tra 0");
  write(done[1], "d", 1);
  char out[3] = {9, 9, 9};
  read(res[0], out, 3);
  int st = 0;
  wait(&st);
  check(st == 0, "nan nhan song sot va thoat binh thuong");
  check(out[0] == 0, "khi con chan: handler chua chay lan nao");
  check(out[1] == 1 && out[2] == 1,
        "mo chan: SIGUSR1 va SIGUSR2 moi loai chay dung 1 lan");
  close(done[1]);
  close(res[0]);

  // 11b: nhiều tiến trình đồng thời gửi SIGKILL cho cùng một nhóm nạn nhân
  int victims[4];
  for (int v = 0; v < 4; v++) {
    victims[v] = fork();
    if (victims[v] == 0) {
      for (;;)
        spin(1000);
    }
  }
  for (int s = 0; s < NSENDER; s++) {
    if (fork() == 0) {
      for (int v = 0; v < 4; v++)
        kill(victims[v], SIGKILL);
      exit(0);
    }
  }
  int nkilled = 0, nsender = 0;
  for (int i = 0; i < 4 + NSENDER; i++) {
    int st2 = 0;
    int w = wait(&st2);
    int isvictim = 0;
    for (int v = 0; v < 4; v++)
      if (w == victims[v])
        isvictim = 1;
    if (isvictim && st2 == -1)
      nkilled++;
    else if (!isvictim && st2 == 0)
      nsender++;
  }
  check(nkilled == 4 && nsender == NSENDER,
        "SIGKILL dong thoi: 4 nan nhan chet (-1), sender thoat binh thuong");
}

// Test 13 (DL-14): gửi signal tới tiến trình ZOMBIE (đã exit, cha chưa wait)
static void
test_zombie(void)
{
  printf("Test 13 (DL-14): gui signal toi tien trinh ZOMBIE\n");
  int pid = fork();
  if (pid == 0)
    exit(7); // con thoát ngay -> thành ZOMBIE
  pause(5);  // cha ngủ 5 tick, CHƯA wait, để con kịp exit
  check(kill(pid, SIGUSR1) == -1, "kill(zombie, SIGUSR1) == -1");
  int status = 0;
  int w = wait(&status);
  check(w == pid && status == 7, "wait() van nhan dung exit status 7");
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
  test_signal_coalescing();
  test_signal_priority();
  printf("Test 9 (DL-09): SIG_IGN khong ket thuc tien trinh\n");
  set_handler(SIGUSR1, SIG_IGN);
  kill(getpid(), SIGUSR1);
  check(1, "kill(self, SIGUSR1) khi SIG_IGN: tien trinh van chay tiep");
  set_handler(SIGUSR1, SIG_DFL);
  test_no_interrupt(SIGUSR1, 0, 3, "SIG_IGN: read() van cho va nhan du lieu");
  test_interrupt_with_handler();
  test_no_lost_wakeup();
  test_multicpu_stress();
  printf("Test 12 (DL-12): signal bi chan khong ngat read() dang cho\n");
  test_no_interrupt(SIGUSR2, 1, 4, "bi chan: read() van cho va nhan du lieu");
  test_zombie();

  printf("=== Ket qua: %d PASS, %d FAIL ===\n", passed, failed);
  if (failed == 0)
    printf("killtest: ALL PASSED\n");
  exit(failed == 0 ? 0 : 1);
}
