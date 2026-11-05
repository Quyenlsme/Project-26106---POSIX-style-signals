#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (sig_interrupted(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;
  int sig;

  argint(0, &pid);
  argint(1, &sig);
  return kkill(pid, sig);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// --- Signal syscall stubs (Tuần 2: chỉ là khung, trả 0) ---
uint64
sys_sigaction(void)
{
  int sig;
  uint64 act_addr;
  uint64 oldact_addr;
  struct proc *p = myproc();

  argint(0, &sig);
  argaddr(1, &act_addr);
  argaddr(2, &oldact_addr);

  // Kiểm tra số hiệu signal và không cho phép can thiệp SIGKILL
  if (sig <= 0 || sig >= NSIG || sig == SIGKILL)
    return -1;

  // Nếu oldact != NULL, copy cấu hình cũ ra user
  if (oldact_addr != 0) {
    if (copyout(p->pagetable, p->sz, oldact_addr, (char *)&p->sig_actions[sig],
                sizeof(struct sigaction)) < 0)
      return -1;
  }

  // Nếu act != NULL, copy cấu hình mới từ user vào kernel
  if (act_addr != 0) {
    struct sigaction newact;
    if (copyin(p->pagetable, p->sz, (char *)&newact, act_addr,
               sizeof(struct sigaction)) < 0)
      return -1;

    // Chỉ chấp nhận flags == 0
    if (newact.sa_flags != 0)
      return -1;

    // Nếu là handler thật (khác DFL và IGN), bắt buộc phải có sa_restorer
    if (newact.sa_handler != SIG_DFL && newact.sa_handler != SIG_IGN) {
      if (newact.sa_restorer == 0)
        return -1;
    }

    // Lọc mask bổ sung an toàn qua sig_sanitize
    newact.sa_mask = sig_sanitize(newact.sa_mask);

    // Cập nhật cấu hình vào bảng của tiến trình (private, không cần p->lock)
    p->sig_actions[sig] = newact;
  }

  return 0;
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();
  struct sigframe sf;
  uint64 frame_addr = p->trapframe->sp;

  // 1. Kiểm tra tính hợp lệ của địa chỉ frame trên user stack:
  // Căn lề 16-byte và nằm hoàn toàn trong [0, p->sz)
  if ((frame_addr & 0xF) != 0 || frame_addr >= p->sz ||
      frame_addr + sizeof(sf) > p->sz) {
    kexit(-1);
  }

  // 2. Đọc sigframe từ user stack vào kernel
  if (copyin(p->pagetable, p->sz, (char *)&sf, frame_addr, sizeof(sf)) < 0) {
    kexit(-1);
  }

  // 3. Bảo toàn các trường cấu hình bất biến của Kernel nội bộ
  uint64 kernel_satp = p->trapframe->kernel_satp;
  uint64 kernel_sp = p->trapframe->kernel_sp;
  uint64 kernel_trap = p->trapframe->kernel_trap;
  uint64 kernel_hartid = p->trapframe->kernel_hartid;

  // Khôi phục lại toàn bộ 32 thanh ghi CPU của chương trình chính
  *(p->trapframe) = sf.saved_tf;

  // Phục hồi lại các trường kernel
  p->trapframe->kernel_satp = kernel_satp;
  p->trapframe->kernel_sp = kernel_sp;
  p->trapframe->kernel_trap = kernel_trap;
  p->trapframe->kernel_hartid = kernel_hartid;

  // 4. Khôi phục lại mặt nạ tín hiệu cũ của tiến trình
  p->blocked_signals = sig_sanitize(sf.saved_mask);

  return p->trapframe->a0;
}

uint64
sys_sigprocmask(void)
{
  int how;
  uint64 set_addr;
  uint64 oldset_addr;
  struct proc *p = myproc();

  argint(0, &how);
  argaddr(1, &set_addr);
  argaddr(2, &oldset_addr);

  sigset_t in_set = 0;
  // Nếu set != NULL, kiểm tra tính hợp lệ của how và nạp mask từ user
  if (set_addr != 0) {
    if (how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)
      return -1;
    if (copyin(p->pagetable, p->sz, (char *)&in_set, set_addr, sizeof(in_set)) <
        0)
      return -1;
  }

  // Nếu oldset != NULL, sao chép mask cũ ra user trước khi đổi
  if (oldset_addr != 0) {
    sigset_t old_mask = p->blocked_signals;
    if (copyout(p->pagetable, p->sz, oldset_addr, (char *)&old_mask,
                sizeof(old_mask)) < 0)
      return -1;
  }

  // Áp dụng mask mới
  if (set_addr != 0) {
    sigset_t sanitized_set = sig_sanitize(in_set);

    if (how == SIG_BLOCK)
      p->blocked_signals = sig_sanitize(p->blocked_signals | sanitized_set);
    else if (how == SIG_UNBLOCK)
      p->blocked_signals = sig_sanitize(p->blocked_signals & ~sanitized_set);
    else if (how == SIG_SETMASK)
      p->blocked_signals = sanitized_set;
  }

  return 0;
}

uint64
sys_alarm(void)
{
  int ticks_count;
  struct proc *p = myproc();

  argint(0, &ticks_count);
  if (ticks_count < 0)
    return -1;

  // Đọc ticks an toàn dưới tickslock
  acquire(&tickslock);
  uint64 now = ticks;
  release(&tickslock);

  acquire(&p->lock);
  uint64 remaining = 0;
  if (p->alarm_deadline > 0) {
    if (p->alarm_deadline > now)
      remaining = p->alarm_deadline - now;
    else
      remaining = 0;
  }

  if (ticks_count == 0) {
    p->alarm_deadline = 0; // Hủy alarm
  } else {
    p->alarm_deadline = now + ticks_count; // Cài đặt deadline mới
  }
  release(&p->lock);

  return remaining;
}
