# Đặc tả Thiết kế Kỹ thuật: Signal Handler & Context Restoration

* **Thành viên phụ trách:** Trần Việt Hưng  
* **Đề tài:** POSIX-style signals trên xv6-riscv (Mã đề tài: 26106)  
* **Vai trò module:** Thiết lập cấu hình hành vi (`sigaction`), dựng khung ngăn xếp tín hiệu (`sigframe`) trên user stack theo chuẩn RISC-V Calling Convention, và khôi phục nguyên vẹn 100% ngữ cảnh tiến trình (`sigreturn`).

---

## 1. Bản chất Kỹ thuật: Thách thức Chuyển ngữ cảnh sang User Handler

Khi kernel quyết định chuyển giao một tín hiệu tới hàm xử lý của người dùng (`handler`), CPU đang ở trong chế độ **Supervisor mode** (Kernel mode), còn hàm handler lại là một đoạn mã nằm trong không gian người dùng (**User mode**).

Để thực thi handler một cách an toàn và sau đó cho phép chương trình chính tiếp tục chạy thông suốt, kernel phải giải quyết 3 bài toán kỹ thuật cốt lõi:

### 1.1. Xung đột Thanh ghi Phần cứng (Register Clobbering)
Hàm signal handler là một hàm C thông thường. Khi compiler biên dịch handler, nó tự do phát sinh mã sử dụng các thanh ghi phần cứng (`a0-a7`, `t0-t6`, `s0-s11`,...).
- Nếu kernel chỉ đơn giản thay đổi con trỏ lệnh `epc` để nhảy vào handler, **toàn bộ dữ liệu tính toán dở dang của chương trình chính trên các thanh ghi CPU sẽ bị handler ghi đè**.
- Khi handler kết thúc, chương trình chính không thể chạy tiếp và sẽ lập tức sụp đổ (crash) hoặc cho ra kết quả sai lệch.
- **Giải pháp:** Kernel bắt buộc phải chụp lại ảnh toàn bộ 32 thanh ghi CPU tại thời điểm ngắt (lưu trong `struct trapframe`) và cất giữ an toàn trước khi trao quyền cho handler.

### 1.2. Vị trí Lưu trữ Ngữ cảnh: Tại sao là User Stack?
Tại sao kernel không lưu ngữ cảnh cũ vào một biến trong `struct proc` ở bộ nhớ kernel, mà lại lưu lên **User Stack**?
- **Hỗ trợ Tín hiệu Lồng nhau (Nested Handlers):** Nếu trong lúc đang chạy handler A, một tín hiệu B khác xuất hiện và được phép thực thi, kernel sẽ tiếp tục ngắt handler A để chạy handler B. Nếu lưu trong kernel struct cố định, ngữ cảnh của A sẽ bị B ghi đè!
- **Cơ chế Ngăn xếp (LIFO - Last In, First Out):** Bằng cách đẩy các khung dữ liệu (`sigframe`) liên tiếp lên chính User Stack của tiến trình, mỗi lần gọi `sigreturn()`, kernel sẽ "bóc" một tầng frame trên cùng ra để khôi phục. Điều này cho phép hỗ trợ lồng nhau nhiều tầng một cách tự nhiên mà không tốn bộ nhớ kernel.

### 1.3. Chuẩn RISC-V ABI và Địa chỉ Quay về Trampoline
Sau khi handler kết thúc (bằng lệnh `ret` trong assembly), CPU sẽ nhảy tới địa chỉ được chỉ định bởi thanh ghi liên kết `ra` (Return Address).
- Nhưng handler không biết làm thế nào để thông báo cho kernel biết nó đã chạy xong để khôi phục lại ngữ cảnh cũ.
- **Giải pháp:** Kernel nạp vào thanh ghi `ra` địa chỉ của một đoạn mã đặc biệt gọi là **Restorer Trampoline** (`sa_restorer`). Khi handler thực hiện lệnh `ret`, CPU tự động nhảy tới `sa_restorer`, tại đây sẽ thực hiện lệnh `ecall` để gọi system call `sigreturn()`, yêu cầu kernel khôi phục lại toàn bộ trạng thái ban đầu.

---

## 2. Thiết kế Khung Ngăn xếp Tín hiệu: `struct sigframe`

### 2.1. Cấu trúc `struct sigframe` (`kernel/signal.h`)
Khung dữ liệu này được kernel trực tiếp ghi vào vùng nhớ user stack của tiến trình:

```c
struct sigframe {
  struct trapframe saved_tf;   // Chụp lại toàn bộ 32 thanh ghi CPU trước khi bị ngắt
  sigset_t saved_mask;         // Mặt nạ tín hiệu cũ của tiến trình
};
```

**Ghi chú thiết kế:** Cấu trúc này không cần lưu trường `signum` và `ret_addr` bên trong khung bộ nhớ, vì:
- Số hiệu tín hiệu (`signum`) được truyền trực tiếp qua thanh ghi tham số `a0`.
- Địa chỉ trở về (`sa_restorer`) được nạp trực tiếp vào thanh ghi liên kết `ra`.

### 2.2. Sơ đồ Căn chỉnh Bộ nhớ User Stack (16-Byte Alignment)
Chuẩn RISC-V Calling Convention quy định con trỏ ngăn xếp (`sp`) luôn luôn phải được **căn lề 16-byte** (`sp % 16 == 0`).

```
Địa chỉ bộ nhớ cao
       │
       │   ┌──────────────────────────────────────────────┐
       │   │   Dữ liệu Ngăn xếp của Chương trình chính   │
       │   └──────────────────────────────────────────────┘
       │                          ▲
       │                          │  (sp ban đầu)
       │   ┌──────────────────────┴───────────────────────┐
       │   │  struct sigframe:                            │
       │   │    - saved_tf (epc, ra, sp, a0-a7, s0-s11...)│
       │   │    - saved_mask                              │
       │   └──────────────────────────────────────────────┘
       │                          ▲
       │                          │  new_sp = (sp_cũ - sizeof(sigframe)) & ~0xF
       │   ┌──────────────────────┴───────────────────────┐
       │   │  Vùng nhớ trống dành riêng cho hàm Handler  │
       │   │  cấp phát biến cục bộ và gọi hàm con         │
       │   └──────────────────────────────────────────────┘
       ▼
Địa chỉ bộ nhớ thấp
```

---

## 3. Thiết kế System Call Cấu hình: `sigaction()`

### 3.1. Cấu trúc `struct sigaction` (`kernel/signal.h`)
```c
struct sigaction {
  void (*sa_handler)(int);     // Con trỏ hàm xử lý, hoặc SIG_DFL, hoặc SIG_IGN
  sigset_t sa_mask;            // Các tín hiệu bị chặn bổ sung trong thời gian handler chạy
  int sa_flags;                // Cờ điều khiển hành vi (hiện tại bắt buộc bằng 0)
  void (*sa_restorer)(void);   // Địa chỉ đoạn mã user thực hiện gọi sigreturn()
};
```

### 3.2. Thuật toán Xử lý trong Kernel (`ksigaction`)
```text
ksigaction(sig, act_user_addr, oldact_user_addr):
    1. Kiểm tra tính hợp lệ:
       - sig <= 0 || sig >= NSIG: trả về -1 (số hiệu không tồn tại).
       - sig == SIGKILL: trả về -1 (chuẩn POSIX nghiêm cấm thay đổi handler của SIGKILL).

    2. Đọc cấu hình cũ ra user (nếu oldact_user_addr != 0):
       - Sao chép p->sig_actions[sig] ra vùng nhớ user qua copyout().
       - Nếu lỗi bộ nhớ: trả về -1.

    3. Nạp cấu hình mới từ user vào kernel (nếu act_user_addr != 0):
       - Sao chép từ user vào biến tạm newact qua copyin().
       - Kiểm tra tính toàn vẹn:
           + newact.sa_flags != 0: trả về -1 (từ chối flags không hỗ trợ).
           + Nếu handler là hàm thật (khác SIG_DFL và SIG_IGN) nhưng sa_restorer == 0:
                 trả về -1 (bắt buộc phải có restorer để quay về an toàn).
       - Chuẩn hóa mask bổ sung:
           newact.sa_mask = sig_sanitize(newact.sa_mask);
       - Ghi đè vào bảng của tiến trình:
           p->sig_actions[sig] = newact;

    4. Trả về 0 (thành công).
```

---

## 4. Dựng Khung Ngăn xếp: Hàm `sig_setup_frame()`

Khi module Delivery của Quyền quyết định kích hoạt một User Handler, hàm `sig_setup_frame()` sẽ được triệu gọi để chuẩn bị ngữ cảnh:

```c
int sig_setup_frame(int sig, struct sigaction *act) {
    struct proc *p = myproc();
    struct sigframe sf;
    
    // Bước 1: Tính toán địa chỉ stack mới và ép căn lề 16-byte
    uint64 new_sp = (p->trapframe->sp - sizeof(struct sigframe)) & ~0xFULL;
    
    // Bước 2: Kiểm tra an toàn bộ nhớ (Boundary Check)
    // Con trỏ stack mới phải nằm trong không gian địa chỉ hợp lệ của tiến trình: [0, p->sz)
    if (new_sp >= p->sz || new_sp < sizeof(struct sigframe))
        return -1;
        
    // Bước 3: Đóng gói ngữ cảnh cũ vào sigframe
    sf.saved_tf = *(p->trapframe);
    sf.saved_mask = p->blocked_signals;
    
    // Bước 4: Sao chép khung sigframe lên User Stack
    if (copyout(p->pagetable, new_sp, (char*)&sf, sizeof(sf)) < 0)
        return -1;
        
    // Bước 5: Cập nhật mặt nạ tín hiệu mới khi vào handler:
    // Tự động chặn: chính tín hiệu đang xử lý + các tín hiệu trong sa_mask
    p->blocked_signals = sig_sanitize(p->blocked_signals | act->sa_mask | (1U << sig));
    
    // Bước 6: Cài đặt các thanh ghi phần cứng chuẩn bị vào Handler theo RISC-V ABI:
    p->trapframe->sp = new_sp;                    // Con trỏ ngăn xếp mới
    p->trapframe->epc = (uint64)act->sa_handler;  // Điểm bắt đầu thực thi của Handler
    p->trapframe->ra = (uint64)act->sa_restorer;  // Điểm nhảy về khi handler thực hiện 'ret'
    p->trapframe->a0 = sig;                       // Tham số đầu tiên của hàm handler (signum)
    
    return 0;
}
```

---

## 5. Khôi phục Ngữ cảnh: System Call `sigreturn()`

### 5.1. Thiết kế Hàm `sys_sigreturn()` và Xử lý Lỗi Nghiêm ngặt
Khi handler kết thúc, nó nhảy vào `sa_restorer` để thực hiện system call `sigreturn()`. Kernel thực hiện giải mã stack frame:

```c
uint64 sys_sigreturn(void) {
    struct proc *p = myproc();
    struct sigframe sf;
    uint64 frame_addr = p->trapframe->sp;
    
    // Bước 1: Kiểm tra tính hợp lệ của địa chỉ stack frame
    // Phải căn lề 16-byte và nằm hoàn toàn trong không gian hợp lệ [0, p->sz)
    if ((frame_addr & 0xF) != 0 || frame_addr >= p->sz || frame_addr + sizeof(sf) > p->sz) {
        // PHÁT HIỆN LỖ HỔNG: Frame bị giả mạo hoặc stack bị hỏng
        // Tuyệt đối không cho tiến trình chạy tiếp với trạng thái sai lệch!
        kexit(-1);
    }
    
    // Bước 2: Đọc dữ liệu sigframe từ user stack vào kernel
    if (copyin(p->pagetable, (char*)&sf, frame_addr, sizeof(sf)) < 0) {
        kexit(-1);
    }
    
    // Bước 3: Bảo toàn các trường cấu hình Kernel nội bộ
    // Không cho phép dữ liệu từ user space ghi đè lên các con trỏ trang và stack của kernel!
    uint64 kernel_satp = p->trapframe->kernel_satp;
    uint64 kernel_sp = p->trapframe->kernel_sp;
    uint64 kernel_trap = p->trapframe->kernel_trap;
    uint64 kernel_hartid = p->trapframe->kernel_hartid;
    
    // Khôi phục toàn bộ 32 thanh ghi CPU cũ của chương trình chính
    *(p->trapframe) = sf.saved_tf;
    
    // Phục hồi lại các trường cấu hình kernel bất biến
    p->trapframe->kernel_satp = kernel_satp;
    p->trapframe->kernel_sp = kernel_sp;
    p->trapframe->kernel_trap = kernel_trap;
    p->trapframe->kernel_hartid = kernel_hartid;
    
    // Bước 4: Khôi phục lại mặt nạ tín hiệu cũ của tiến trình
    p->blocked_signals = sig_sanitize(sf.saved_mask);
    
    return p->trapframe->a0;
}
```

### 5.2. Ngăn chặn Dispatcher ghi đè Thanh ghi `a0`
Trong kiến trúc gốc của xv6 (`kernel/syscall.c`), dispatcher luôn thực hiện lệnh gán:
```c
p->trapframe->a0 = syscalls[num]();
```
- **Hậu quả nếu không xử lý:** Khi `sys_sigreturn()` khôi phục xong giá trị `a0` cũ của chương trình chính (chứa giá trị biến cục bộ hoặc kết quả tính toán trước khi bị ngắt), dispatcher sẽ ghi đè giá trị trả về của `sys_sigreturn()` vào `trapframe->a0`. Điều này làm hỏng biến `a0` của chương trình chính!
- **Giải pháp bảo toàn 100%:** Trong `syscall()` tại `kernel/syscall.c`, thêm nhánh xử lý đặc biệt cho `SYS_sigreturn`:
```c
if (num == SYS_sigreturn) {
    sys_sigreturn(); // Gọi hàm khôi phục nhưng không gán lại p->trapframe->a0
} else {
    p->trapframe->a0 = syscalls[num]();
}
```

---

## 6. Cơ chế Xử lý Tín hiệu Lồng nhau (Nested Handlers)

```
        USER STACK
 ┌──────────────────────┐
 │     Main Program     │
 ├──────────────────────┤
 │  sigframe (Signal A) │ ◄── Handler A đang chạy, bị Signal B ngắt
 ├──────────────────────┤
 │  sigframe (Signal B) │ ◄── Handler B được dựng đè bên dưới (LIFO)
 ├──────────────────────┤
 │   Handler B Stack    │ ◄── sp hiện tại đang chạy Handler B
 └──────────────────────┘
```

1. Khi đang thực thi Handler A, nếu có Signal B đến (và B không nằm trong mặt nạ `blocked_signals` hiện hành):
   - Con trỏ `sp` tiếp tục được giảm xuống thêm một khoảng `sizeof(struct sigframe)`.
   - Khung `sigframe` của B được tạo đè lên dưới khung của A.
   - CPU chuyển sang chạy Handler B.
2. Handler B kết thúc -> gọi `sigreturn()`:
   - Kernel giải phóng khung của B, khôi phục lại trạng thái thanh ghi và mặt nạ tín hiệu của Handler A.
   - CPU quay trở lại thực thi tiếp phần còn lại của Handler A.
3. Handler A kết thúc -> gọi `sigreturn()` lần 2:
   - Kernel giải phóng khung của A, khôi phục lại trạng thái ban đầu của chương trình chính.
4. **Cơ chế chống tràn ngăn xếp (Recursion Prevention):** Nhờ cơ chế tự động đưa chính tín hiệu đang xử lý vào mặt nạ chặn (`blocked_signals |= (1U << sig)`), nếu một tín hiệu cùng loại liên tục gửi đến, nó sẽ bị giữ ở trạng thái `pending` chứ không kích hoạt lồng đệ quy vô hạn làm sập stack.
5. **Giới hạn độ sâu lồng nhau (Stack Limit):**
   - Cơ chế nested handler sử dụng chính User Stack của tiến trình để lưu các `sigframe`.
   - Trong thiết kế hiện tại, mỗi tiến trình có `USERSTACK = 1` (một trang User Stack).
   - Mỗi lần phát sinh nested handler, kernel cần dành thêm không gian trên User Stack để lưu `sigframe` mới.
   - Do dung lượng User Stack có giới hạn, độ sâu nested handler cũng bị giới hạn bởi không gian stack còn lại.
   - Nếu không đủ không gian để đặt `sigframe`, thao tác dựng signal frame thất bại và tiến trình bị kết thúc với trạng thái `-1`, thay vì làm kernel panic.
---

## 7. Ma trận Kiểm thử Module Handler (HD-01 đến HD-13)

| Mã Test | Tình huống kiểm thử | Hành động & Dữ liệu đầu vào | Kết quả mong đợi |
|---|---|---|---|
| **HD-01** | Đăng ký Handler hợp lệ | Gọi `sigaction` cài đặt handler cho `SIGUSR1`, đọc `oldact` | Đăng ký thành công, `oldact` trả về cấu hình trước đó |
| **HD-02** | Từ chối can thiệp SIGKILL | Gọi `sigaction(SIGKILL, &act, 0)` | Trả về `-1`, handler của `SIGKILL` bất biến |
| **HD-03** | Bảo toàn 100% thanh ghi CPU | Nạp các số nguyên ngẫu nhiên vào `s0-s11`, `a1-a7`, nhận signal | Sau khi handler kết thúc, tất cả thanh ghi giữ nguyên giá trị |
| **HD-04** | Bảo toàn thanh ghi `a0` | Chương trình chính đang giữ giá trị `a0 = 0x1234` | Sau `sigreturn()`, `a0` vẫn giữ nguyên `0x1234` |
| **HD-05** | Khôi phục Program Counter | Đang thực hiện vòng lặp tính toán thì bị ngắt | Handler kết thúc, vòng lặp chạy tiếp đúng vị trí lệnh bị dừng |
| **HD-06** | Khôi phục Signal Mask | Trước handler mask = M1; trong handler mask = M2 | Sau `sigreturn()`, mask được khôi phục chính xác về M1 |
| **HD-07** | Nested Handlers 2 tầng | Handler `SIGUSR1` nhận tiếp `SIGUSR2` | Handler 2 chạy và unwind trước, sau đó Handler 1 hoàn tất |
| **HD-08** | Chống đệ quy lặp cùng signal | Trong handler `SIGUSR1`, gửi lại chính `SIGUSR1` | Signal mới bị pending, không bị lồng đệ quy tràn stack |
| **HD-09** | Xử lý Bad Stack Frame | Cố tình gọi `sigreturn()` với con trỏ stack không căn lề hoặc trỏ ra ngoài bộ nhớ | Kernel gọi `kexit(-1)` diệt tiến trình an toàn, không panic |
| **HD-10** | Căn lề Stack 16-byte | Đọc giá trị thanh ghi `sp` ngay dòng đầu tiên của hàm handler | `sp % 16 == 0` tuyệt đối |
| **HD-11** | Nested Handlers 3 tầng | Handler `SIGUSR1` gửi tiếp `SIGUSR2`; Handler `SIGUSR2` gửi tiếp `SIGALRM` | Ba handler được thực thi đúng thứ tự lồng nhau; unwind theo LIFO; signal mask được khôi phục chính xác |
| **HD-12** | User Stack không đủ chỗ chứa Signal Frame | Cố tình đặt `sp` tại vị trí không đủ không gian để kernel ghi `sigframe` | Tiến trình con kết thúc với trạng thái `-1`; kernel không panic |
| **HD-13** | Burst / Multiple Pending Signals | Chặn `SIGUSR1`, `SIGUSR2`, `SIGALRM`, gửi theo thứ tự ngược rồi bỏ chặn | Cả ba signal được xử lý đúng một lần theo thứ tự số hiệu tăng dần; các thanh ghi `s1-s11` được bảo toàn |