// kernel/signal.h — Định nghĩa dùng chung cho hệ thống signal

#ifndef SIGNAL_H
#define SIGNAL_H

#include "types.h"

// --- Signal numbers ---
#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGKILL 9
#define SIGUSR1 10
#define SIGUSR2 12
#define SIGALRM 14
#define SIGTERM 15

#define NSIG 32 // Tổng số signal tối đa(không dùng bit 0, hợp lệ: 1..31)

// --- Signal mask type ---
typedef uint32 sigset_t;

// --- Đặc biệt handler ---
#define SIG_DFL ((void (*)(int))0) // Hành vi mặc định (POSIX standard)
#define SIG_IGN ((void (*)(int))1) // Bỏ qua signal (POSIX standard)

// --- Signal mask operations (dùng cho sigprocmask) ---
#define SIG_BLOCK   0 // blocked |= set
#define SIG_UNBLOCK 1 // blocked &= ~set
#define SIG_SETMASK 2 // blocked  = set

// --- Mask hằng số ---
// Signal được dự án hỗ trợ (bật bit cho từng signal ở trên)
#define SUPPORTED_MASK                                                         \
  ((1U << SIGHUP) | (1U << SIGINT) | (1U << SIGQUIT) | (1U << SIGKILL) |       \
   (1U << SIGUSR1) | (1U << SIGUSR2) | (1U << SIGALRM) | (1U << SIGTERM))

// Signal không thể bị chặn hoặc ignore
#define UNBLOCKABLE_MASK (1U << SIGKILL)

// --- Cấu trúc signal action ---
struct sigaction {
  void (*sa_handler)(int); // Con trỏ hàm handler, SIG_DFL hoặc SIG_IGN
  sigset_t sa_mask;        // Signal bị chặn bổ sung khi handler đang chạy
};

// --- Cấu trúc sigframe (lưu trên user stack) ---
struct sigframe {
  struct trapframe saved_tf; // Toàn bộ ngữ cảnh thanh ghi trước khi nhận signal
  sigset_t saved_mask;       // Signal mask cũ của tiến trình
};

#endif // SIGNAL_H
