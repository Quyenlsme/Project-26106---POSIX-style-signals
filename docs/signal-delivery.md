# Đặc tả Thiết kế Kỹ thuật: Signal Delivery & Sending

* **Thành viên phụ trách:** Phạm Lê Quyền  
* **Đề tài:** POSIX-style signals trên xv6-riscv (Mã đề tài: 26106)  
* **Vai trò module:** Quản lý hàng đợi tín hiệu đang chờ (`pending_signals`), cơ chế gửi tín hiệu bất đồng bộ (`kill`), lựa chọn tín hiệu và điều phối chuyển giao (`delivery`) từ kernel về user mode.

---

## 1. Bản chất Kiến trúc: Tại sao Signal cần cơ chế Hai Pha (Two-Phase)?

Tín hiệu (signal) là một cơ chế ngắt phần mềm (software interrupt) bất đồng bộ. Khi một tiến trình gọi `kill(pid, sig)` để gửi tín hiệu tới tiến trình đích:

> **Kernel tuyệt đối không thể chuyển ngay lập tức sang chạy hàm handler của tiến trình đích.**

Nguyên nhân kỹ thuật:
1. **Tiến trình đích có thể đang ngủ (`SLEEPING`):** Ví dụ tiến trình đang chờ dữ liệu từ đĩa hoặc console, nó chưa được CPU nạp vào thực thi.
2. **Tiến trình đích có thể đang chạy trong Kernel Context:** Nếu tiến trình đích đang thực hiện một system call quan trọng, việc ngắt ngang và ép chạy mã người dùng sẽ làm hỏng trạng thái kernel stack và gây đổ vỡ hệ thống.
3. **An toàn ranh giới đặc quyền (Privilege Boundary):** Không được phép thực thi mã người dùng (user code) khi CPU đang ở chế độ đặc quyền Supervisor mode.

Vì vậy, kiến trúc xử lý tín hiệu được chia thành **hai pha độc lập**:
- **Pha 1: Tiếp nhận và ghi nhận (Signal Sending/Enqueueing):** Khi `kill(pid, sig)` được gọi, kernel chỉ ghi nhận sự kiện bằng cách bật bit tương ứng trong `p->pending_signals` của tiến trình nhận, đồng thời đánh thức tiến trình đó nếu nó đang ngủ.
- **Pha 2: Chuyển giao và kích hoạt (Signal Delivery):** Khi tiến trình nhận được lập lịch chạy, hoàn thành công việc trong kernel và chuẩn bị quay trở lại user mode (`prepare_return`), kernel mới kiểm tra danh sách pending, chọn tín hiệu và chuyển hướng luồng thực thi sang handler.

---

## 2. Mô hình Dữ liệu và Nguyên tắc Đồng bộ (Synchronization)

### 2.1. Cấu trúc dữ liệu trong `struct proc` (`kernel/proc.h`)
```c
struct proc {
  // ... các trường gốc của xv6 ...
  
  // Được bảo vệ bởi p->lock:
  sigset_t pending_signals;            // Bitmask các tín hiệu đang chờ xử lý
  uint64 alarm_deadline;               // Hạn chót timer (ticks)

  // Dữ liệu Private của tiến trình (chỉ tiến trình hiện hành truy cập, không cần p->lock):
  sigset_t blocked_signals;            // Bitmask các tín hiệu đang bị chặn (mask)
  struct sigaction sig_actions[NSIG];  // Bảng cấu hình hành vi cho từng tín hiệu
};
```

### 2.2. Biểu diễn Bitmask và Quy ước Signal Number
Kiểu `sigset_t` là một số nguyên không dấu 32-bit (`uint32`):
- Các tín hiệu hợp lệ nằm trong khoảng: `1 <= sig < NSIG (32)`.
- Bit `0` không sử dụng.
- Tín hiệu số `sig` tương ứng với bit `(1U << sig)`. Ví dụ: `SIGHUP` (1) là bit 1, `SIGINT` (2) là bit 2, `SIGKILL` (9) là bit 9.
- **Đặc tính gộp (Coalescing):** Nếu cùng một tín hiệu được gửi nhiều lần liên tiếp khi tiến trình chưa kịp xử lý, bitmask chỉ giữ một bit `1`. Kernel không lưu hàng đợi vô hạn, ngăn chặn tràn bộ nhớ.

### 2.3. Quy tắc Khóa (Locking Invariant)
- **`pending_signals`:** Bắt buộc phải giữ `p->lock` khi đọc và ghi, vì trường này có thể bị ghi đồng thời bởi CPU khác (tiến trình khác gửi tín hiệu) hoặc bởi ngắt phần cứng (ngắt timer gửi `SIGALRM`).
- **`blocked_signals` và `sig_actions`:** Là dữ liệu riêng (private) của tiến trình. Chỉ bản thân tiến trình đó mới có quyền tự thay đổi mặt nạ hoặc handler của chính nó. Do đó, việc đọc bảng action và mask trong quá trình delivery không cần giữ `p->lock`, giảm tranh chấp khóa giữa các CPU.

---

## 3. Cơ chế Gửi Tín hiệu: `kkill(pid, sig)` và `sig_send(p, sig)`

### 3.1. Chữ ký System Call
- Phía user: `int kill(int pid, int sig);` (khai báo tại `user/user.h`).
- Phía kernel: `int kkill(int pid, int sig);` (khai báo tại `kernel/defs.h`).
- System call dispatcher: `sys_kill()` trong `kernel/sysproc.c` trích xuất `pid` và `sig` từ thanh ghi `a0`, `a1` rồi gọi `kkill()`.

### 3.2. Thuật toán xử lý trong Kernel
```text
kkill(pid, sig):
    1. Kiểm tra tham số:
       - pid <= 0: từ chối (xv6 baseline không hỗ trợ process group).
       - sig <= 0 || sig >= NSIG: từ chối số hiệu tín hiệu không hợp lệ.
       -> Nếu sai, trả về -1.

    2. Duyệt bảng tiến trình proc[]:
       - Lấy p->lock của từng slot.
       - Kiểm tra: p->pid == pid VÀ p->state != UNUSED VÀ p->state != ZOMBIE.
       - Nếu tìm thấy:
           a. Nếu sig == SIGKILL:
              - Đặt p->killed = 1 (giữ nguyên đường xử lý gốc của xv6 để diệt tiến trình dứt điểm).
              - Nếu p->state == SLEEPING: chuyển sang RUNNABLE.
              - Nhả p->lock và trả về 0.
           b. Nếu là tín hiệu khác:
              - Gọi sig_send(p, sig):
                  + Bật bit: p->pending_signals |= (1U << sig);
                  + Nếu p->state == SLEEPING: chuyển p->state = RUNNABLE (đánh thức tiến trình).
              - Nhả p->lock và trả về 0.
       - Nhả p->lock nếu PID không khớp.

    3. Nếu duyệt hết bảng không tìm thấy PID hợp lệ: trả về -1.
```

**Điểm kỹ thuật mấu chốt:** Việc đánh thức tiến trình đang `SLEEPING` sang `RUNNABLE` là bắt buộc. Nếu không có bước này, một tiến trình đang gọi `pause()` hoặc chờ I/O sẽ tiếp tục ngủ mãi mãi và không bao giờ thức dậy để nhận tín hiệu.

---

## 4. Cơ chế Chuyển giao Tín hiệu: `sig_deliver()` tại `prepare_return()`

### 4.1. Vị trí Kích hoạt Delivery
Trong `kernel/trap.c`, hàm `prepare_return()` là trạm kiểm soát cuối cùng trước khi CPU chuyển giao quyền kiểm soát từ kernel về user mode. Vị trí này áp dụng cho:
- Sau khi hoàn thành một system call.
- Sau khi xử lý xong một ngắt thiết bị hoặc ngắt timer (sau khi đã `yield()`).
- Sau khi tiến trình con mới sinh ra thoát khỏi `forkret()`.

Tại đây, kernel gọi `sig_deliver()` trước lệnh tắt ngắt `intr_off()`.

### 4.2. Thuật toán Điều phối Delivery
```text
sig_deliver():
    p = myproc()
    Nếu không có tiến trình hoặc p->killed == 1:
        kexit(-1) // Ưu tiên kết thúc ngay nếu đã bị đánh dấu killed

    1. LỰA CHỌN TÍN HIỆU (Signal Selection):
       - Giữ p->lock.
       - Tính tập tín hiệu sẵn sàng:
             ready = p->pending_signals & SUPPORTED_MASK & ~sig_sanitize(p->blocked_signals);
       - Nếu ready == 0:
             Nhả p->lock, kết thúc sig_deliver (tiếp tục quay về chương trình chính).
       - Chọn signal nhỏ nhất:
             Duyệt từ bit 1 đến 31, lấy số hiệu i đầu tiên có bit bật.
       
       - CLAIM TÍN HIỆU (Quan trọng):
             Xóa bit khỏi hàng chờ ngay lập tức: p->pending_signals &= ~(1U << i);
       - Nhả p->lock.

    2. XỬ LÝ HÀNH VI (Action Dispatching):
       - Đọc cấu hình: act = p->sig_actions[i];
       
       - Trường hợp 1: act.sa_handler == SIG_IGN (Bỏ qua)
             Không làm gì, kết thúc để tiếp tục luồng user.
             (Lưu ý: SIGKILL không bao giờ rơi vào đây do không thể bị set SIG_IGN).

       - Trường hợp 2: act.sa_handler == SIG_DFL (Hành vi mặc định)
             Theo quy ước thiết kế D5: hầu hết tín hiệu mặc định kết thúc tiến trình:
             kexit(-1);

       - Trường hợp 3: act.sa_handler là hàm do người dùng cài đặt
             Gọi sang module Handler của Hưng để dựng khung stack:
                 res = sig_setup_frame(i, &act);
             Nếu res < 0 (lỗi bộ nhớ, hỏng user stack):
                 kexit(-1); // Kết thúc ngay để tránh sập kernel
             Nếu res == 0:
                 Kết thúc sig_deliver. CPU sẽ nhảy vào thực thi Handler của người dùng!
```

### 4.3. Tại sao phải "Claim Tín hiệu" trước khi dựng Frame?
Thao tác xóa bit `p->pending_signals &= ~(1U << i)` diễn ra dưới `p->lock` ngay trước khi nhả khóa để gọi `sig_setup_frame()`.
- **Nếu xóa trước:** Trong quá trình kernel dựng frame trên user stack (thao tác `copyout` có thể tốn thời gian), nếu một CPU khác gửi tiếp tín hiệu cùng loại, bit pending sẽ được bật lại an toàn và sẽ được xử lý ở lần trở về tiếp theo.
- **Nếu xóa sau:** Tín hiệu mới đến trong lúc dựng frame sẽ bị ghi đè, và sau đó lệnh xóa hoàn tất sẽ xóa mất tín hiệu mới đó, gây ra hiện tượng thất thoát tín hiệu (Signal Dropping).

---

## 5. Tương tác với Phím Ctrl+C từ Bàn phím Console

Nhằm hiện thực hóa kịch bản người dùng nhấn `Ctrl+C` để ngắt chương trình đang chạy trong console:
1. Khi bàn phím gửi ký tự `0x03` (`ETX` - End of Text), hàm `consoleintr()` trong `kernel/console.c` nhận diện được sự kiện này.
2. Kernel tìm kiếm tiến trình đang chạy ở tiền cảnh (foreground process) gắn liền với console.
3. Kernel gọi `kkill(target_pid, SIGINT)` để gửi tín hiệu ngắt.
4. Nếu tiến trình đã đăng ký handler cho `SIGINT`, hàm xử lý sẽ được gọi; nếu chưa đăng ký (`SIG_DFL`), tiến trình sẽ dừng lại sạch sẽ qua `kexit(-1)`.

---

## 6. Sơ đồ Luồng Thực thi (Execution Flow)

```mermaid
sequenceDiagram
    autonumber
    participant Sender as Tiến trình Gửi / Console / Timer
    participant Kernel as Kernel (kkill / sig_send)
    participant ProcState as struct proc
    participant TrapReturn as prepare_return()
    participant Handler as User Signal Handler

    Sender->>Kernel: kill(pid, sig)
    Kernel->>ProcState: acquire(&p->lock)
    Kernel->>ProcState: pending_signals |= (1 << sig)
    alt Tiến trình đang SLEEPING
        Kernel->>ProcState: state = RUNNABLE
    end
    Kernel->>ProcState: release(&p->lock)

    Note over ProcState, TrapReturn: Bộ điều phối đưa tiến trình vào chạy
    TrapReturn->>Kernel: Gọi sig_deliver() trước khi về User
    Kernel->>ProcState: ready = pending & ~blocked
    alt ready != 0 (Có tín hiệu cần xử lý)
        Kernel->>ProcState: pending_signals &= ~(1 << sig) [CLAIM]
        alt Action == SIG_DFL
            Kernel->>Kernel: kexit(-1)
        else Action == Handler thật
            Kernel->>Kernel: sig_setup_frame() (Dựng sigframe, sửa epc/sp/ra/a0)
            Kernel->>Handler: sret chuyển quyền sang User Handler
            Handler->>Kernel: sigreturn() (Khôi phục nguyên vẹn ngữ cảnh cũ)
        end
    else ready == 0
        Kernel->>Handler: sret trở về vị trí cũ của chương trình chính
    end
```

---

## 7. Ma trận Kiểm thử Module Delivery (DL-01 đến DL-11)

| Mã Test | Tình huống kiểm thử | Hành động & Dữ liệu đầu vào | Kết quả mong đợi |
|---|---|---|---|
| **DL-01** | Gửi tín hiệu thông thường | Gửi `SIGUSR1` tới tiến trình có handler | Handler chạy thành công, không làm crash tiến trình |
| **DL-02** | Tín hiệu bị chặn (Masked) | Chặn `SIGUSR1`, sau đó gửi `SIGUSR1` | Handler chưa chạy; bit tồn tại trong `pending_signals` |
| **DL-03** | Delivery sau khi Unmask | Tiếp tục từ DL-02, mở chặn `SIGUSR1` | Handler được gọi thực thi ngay sau khi unmask |
| **DL-04** | Gộp tín hiệu lặp lại | Chặn `SIGUSR1`, gửi 3 lần liên tiếp, mở chặn | Handler chỉ chạy đúng 1 lần duy nhất |
| **DL-05** | Thứ tự ưu tiên tín hiệu | Gửi cùng lúc `SIGUSR2` (12) và `SIGUSR1` (10) | `SIGUSR1` có số hiệu nhỏ hơn được delivery trước |
| **DL-06** | Gửi tới PID không tồn tại | Gọi `kill(9999, SIGUSR1)` | Syscall trả về `-1`, kernel hoạt động ổn định |
| **DL-07** | Số hiệu tín hiệu sai | Gọi `kill(pid, 0)` hoặc `kill(pid, 32)` | Syscall trả về `-1` |
| **DL-08** | Hành vi mặc định `SIG_DFL` | Không đặt handler, gửi `SIGTERM` | Tiến trình kết thúc ngay lập tức với exit code -1 |
| **DL-09** | Hành vi bỏ qua `SIG_IGN` | Đặt `SIG_IGN` cho `SIGUSR1`, gửi signal | Tín hiệu bị tiêu thụ, tiến trình chạy tiếp bình thường |
| **DL-10** | Đánh thức tiến trình SLEEPING | Tiến trình con gọi `sleep()`, cha gửi signal | Tiến trình con được đánh thức dậy để chạy handler |
| **DL-11** | Bắt ngắt Ctrl+C | Chương trình chạy lặp vô tận, nhấn Ctrl+C | `SIGINT` kích hoạt handler thành công |
