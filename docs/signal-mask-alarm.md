# Đặc tả Thiết kế Kỹ thuật: Signal Masking & Timer Alarm

* **Thành viên phụ trách:** Nguyễn Trung Kiên  
* **Đề tài:** POSIX-style signals trên xv6-riscv (Mã đề tài: 26106)  
* **Vai trò module:** Quản lý mặt nạ tín hiệu (`sigprocmask`), cơ chế hẹn giờ tín hiệu bất đồng bộ (`alarm`), tích hợp quét timer interrupt trong kernel, và chuẩn hóa bảo vệ hệ thống (`sig_sanitize`).

---

## 1. Bản chất Kỹ thuật: Masking và Sự khác biệt giữa Blocked và Pending

Trong mô hình tín hiệu POSIX, hai khái niệm **Blocked (Bị chặn)** và **Pending (Đang chờ)** có ý nghĩa hoàn toàn khác nhau về mặt kiến trúc:

| Khái niệm | Ý nghĩa kiến trúc | Nơi lưu trữ | Bản chất điều khiển |
|---|---|---|---|
| **`pending_signals`** | **Sự kiện thực tế:** Tín hiệu đã được gửi tới tiến trình nhưng chưa được xử lý. | Bitmask 32-bit trong `struct proc` (bảo vệ bởi `p->lock`). | Do bên ngoài hoặc phần cứng kích hoạt (bất đồng bộ). |
| **`blocked_signals`** | **Chính sách của tiến trình:** Danh sách các tín hiệu mà tiến trình tạm thời chưa muốn tiếp nhận tại thời điểm hiện tại. | Bitmask 32-bit trong `struct proc` (dữ liệu private của tiến trình). | Do chính bản thân tiến trình chủ động thiết lập qua `sigprocmask()`. |

### Nguyên tắc Vàng của Signal Masking:
> **Tín hiệu bị chặn (Blocked) KHÔNG bị biến mất, mà vẫn được ghi nhận vào `pending_signals`.**

Khi một tín hiệu đang nằm trong `blocked_signals` được gửi tới:
1. Kernel vẫn bật bit tương ứng trong `p->pending_signals`.
2. Nhưng khi tiến trình đi qua `prepare_return()`, kernel lọc theo công thức:
   $$\text{ready} = \text{pending\_signals} \ \& \ \text{SUPPORTED\_MASK} \ \& \ \sim\text{blocked\_signals}$$
   Vì tín hiệu đang bị block, bit đó không xuất hiện trong `ready`, do đó handler **tạm thời chưa được kích hoạt**.
3. Khi tiến trình gọi `sigprocmask(SIG_UNBLOCK)` để gỡ bỏ mặt nạ, ngay ở lần trở về user mode tiếp theo, bit pending sẽ lọt vào tập `ready` và handler được kích hoạt tức thì.

---

## 2. Chuẩn hóa Mặt nạ Tín hiệu: Hàm `sig_sanitize()`

Chuẩn POSIX quy định một số tín hiệu tối cao (đặc biệt là `SIGKILL`) **tuyệt đối không bao giờ được phép chặn hoặc bỏ qua**, nhằm đảm bảo hệ điều hành luôn có khả năng chấm dứt một tiến trình bị treo hoặc độc hại.

Hàm trợ giúp dùng chung trong kernel:
```c
sigset_t sig_sanitize(sigset_t mask) {
    // 1. Chỉ giữ lại các tín hiệu nằm trong tập kernel hỗ trợ
    // 2. Gỡ bỏ bắt buộc bit của các tín hiệu không thể chặn (SIGKILL)
    return (mask & SUPPORTED_MASK) & ~UNBLOCKABLE_MASK;
}
```
Mọi thao tác thay đổi `blocked_signals` từ user-space đều phải đi qua `sig_sanitize()` trước khi được lưu vào `struct proc`.

---

## 3. Thiết kế System Call `sigprocmask()`

### 3.1. Chữ ký và Hằng số Điều khiển
- Giao diện user: `int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);`
- Ba thao tác tiêu chuẩn:
  - `SIG_BLOCK` (0): Bổ sung thêm các tín hiệu vào danh sách chặn: $\text{blocked} \gets \text{blocked} \mid \text{set}$.
  - `SIG_UNBLOCK` (1): Gỡ bỏ các tín hiệu khỏi danh sách chặn: $\text{blocked} \gets \text{blocked} \ \& \ \sim\text{set}$.
  - `SIG_SETMASK` (2): Ghi đè toàn bộ danh sách chặn bằng mặt nạ mới: $\text{blocked} \gets \text{set}$.

### 3.2. Thuật toán Xử lý trong Kernel (`ksigprocmask`)
Để đảm bảo tính toàn vẹn (Atomicity) và an toàn bộ nhớ:
```text
ksigprocmask(how, set_user_addr, oldset_user_addr):
    p = myproc()

    1. Kiểm tra tham số:
       - Nếu set_user_addr != 0:
             Nếu how không thuộc {SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK}:
                 trả về -1.

    2. Lưu mặt nạ cũ ra User (nếu oldset_user_addr != 0):
       - Đọc old_mask = p->blocked_signals;
       - Sao chép ra user qua copyout(..., oldset_user_addr, &old_mask, ...);
       - Nếu copyout thất bại: trả về -1 NGAY LẬP TỨC.
       (Quy tắc an toàn: Không thay đổi trạng thái nếu việc sao lưu thất bại).

    3. Cập nhật mặt nạ mới (nếu set_user_addr != 0):
       - Sao chép dữ liệu từ user vào biến tạm in_set qua copyin().
       - Nếu copyin thất bại: trả về -1.
       - Chuẩn hóa: sanitized_set = sig_sanitize(in_set);
       - Cập nhật vào trường private p->blocked_signals:
           + Nếu how == SIG_BLOCK:   p->blocked_signals = sig_sanitize(p->blocked_signals | sanitized_set);
           + Nếu how == SIG_UNBLOCK: p->blocked_signals = sig_sanitize(p->blocked_signals & ~sanitized_set);
           + Nếu how == SIG_SETMASK: p->blocked_signals = sanitized_set;

    4. Trả về 0 (thành công).
```

**Tính an toàn của `sigprocmask`:**
Hàm này **tuyệt đối không gọi trực tiếp handler**. Khi `sigprocmask` trả về, luồng thực thi đi qua `prepare_return()`. Tại đây, module Delivery sẽ tự động phát hiện các pending signals vừa được giải phóng và kích hoạt handler một cách tự nhiên.

---

## 4. Thiết kế Bộ Hẹn giờ: System Call `alarm()`

### 4.1. Kiến trúc Timer: Tại sao chọn `ticks` Toàn cục thay vì Đếm lùi?
Trong xv6, ngắt timer (`clockintr`) xảy ra định kỳ khoảng 10 lần mỗi giây (khoảng 100ms/tick).
- **Vấn đề khi cho tiến trình tự đếm lùi trong `usertrap()`:** Mỗi CPU core đều nhận timer interrupt độc lập. Nếu một tiến trình tự giảm biến đếm mỗi khi nhận timer interrupt, tốc độ đếm lùi sẽ phụ thuộc vào số lượng CPU của máy và thời gian tiến trình được lập lịch chạy, dẫn đến việc đếm giờ bị sai lệch nghiêm trọng giữa các môi trường chạy (`CPUS=1` so với `CPUS=4`).
- **Giải pháp thiết kế:** Sử dụng biến toàn cục `ticks` của xv6 và lưu trữ **hạn chót tuyệt đối (Absolute Deadline)**:
  $$\text{alarm\_deadline} = \text{ticks} + n$$
  - Giá trị này hoàn toàn độc lập với việc tiến trình đang chạy hay đang ngủ.
  - Kernel không cần cập nhật biến đếm lùi cho từng tiến trình ở mỗi tick, tiết kiệm tối đa chi phí CPU.

### 4.2. Xử lý Hết hạn trong Timer Interrupt (`sig_alarm_check`)
Mỗi khi ngắt đồng hồ phần cứng xảy ra trên CPU 0, hàm `clockintr()` trong `kernel/trap.c` tăng biến toàn cục `ticks`, sau đó gọi `sig_alarm_check(ticks)`:

```c
void sig_alarm_check(uint64 now) {
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        // Chỉ xét các tiến trình đang hoạt động và có cài đặt alarm
        if (p->state != UNUSED && p->state != ZOMBIE && p->alarm_deadline != 0) {
            // So sánh kiểu có dấu để an toàn tuyệt đối khi biến ticks bị tràn số (overflow)
            if ((int64)(now - p->alarm_deadline) >= 0) {
                p->alarm_deadline = 0;                  // Cơ chế One-shot: tự động hủy deadline
                p->pending_signals |= (1U << SIGALRM); // Bật cờ báo tín hiệu SIGALRM đang chờ
                if (p->state == SLEEPING) {
                    p->state = RUNNABLE;                // Đánh thức nếu tiến trình đang ngủ chờ alarm
                }
            }
        }
        release(&p->lock);
    }
}
```

### 4.3. Quy tắc Thứ tự Khóa Tuyệt đối (Lock Ordering Invariant)
Trong kernel xv6, quy tắc thứ tự khóa giữa timer và tiến trình là:
$$\text{tickslock} \longrightarrow p\text{->lock}$$
Trong hàm `kalarm()`, để đọc biến `ticks` và gán `alarm_deadline`, kernel **tuyệt đối không lồng hai khóa này vào nhau**:
```c
// Bước 1: Đọc ticks dưới tickslock rồi nhả ngay
acquire(&tickslock);
uint64 now = ticks;
release(&tickslock);

// Bước 2: Thao tác trên proc dưới p->lock
acquire(&p->lock);
// tính remaining và gán deadline...
release(&p->lock);
```
Thiết kế này triệt tiêu hoàn toàn nguy cơ xảy ra khóa chết (Deadlock) giữa các CPU.

### 4.4. Cơ chế One-shot và Kỹ thuật Hẹn giờ Tuần hoàn (Periodic Re-arming)
Chuẩn POSIX quy định `alarm()` là cơ chế kích hoạt một lần duy nhất (**One-shot**).
- Khi hết hạn, `alarm_deadline` tự động trở về `0`.
- **Nếu muốn tạo đồng hồ chạy tuần hoàn (Periodic Timer):** Người lập trình chỉ cần gọi lại `alarm(n)` ngay bên trong thân của hàm handler xử lý `SIGALRM` (kỹ thuật Re-arming).
- Nếu gọi `alarm(0)`: Hủy bỏ bộ hẹn giờ hiện tại, không có tín hiệu nào được phát ra.

---

## 5. Vòng đời Tiến trình đối với Masking và Alarm

| Thao tác Vòng đời | Xử lý `blocked_signals` | Xử lý `alarm_deadline` | Nguyên lý Thiết kế |
|---|---|---|---|
| **`allocproc()`** | Khởi tạo `= 0` | Khởi tạo `= 0` | Tiến trình mới tạo không bị chặn tín hiệu nào và không có timer. |
| **`kfork()`** | **Thừa kế** từ cha | **Không thừa kế** (`= 0`) | Tiến trình con nhận cùng chính sách bảo vệ tín hiệu của cha; nhưng timer hẹn giờ là riêng biệt cho từng tiến trình. |
| **`kexec()`** | **Giữ nguyên** | **Hủy bỏ** (`= 0`) | Binary mới kế thừa môi trường chặn tín hiệu của tiến trình cũ; nhưng timer của chương trình cũ bị hủy. |
| **`freeproc()`** | Xóa sạch `= 0` | Xóa sạch `= 0` | Giải phóng tài nguyên an toàn khi tiến trình kết thúc. |

---

## 6. Sơ đồ Luồng Hoạt động Tổng thể của Alarm & Mask

```mermaid
sequenceDiagram
    autonumber
    participant App as Chương trình User
    participant Sys as System Call (sys_alarm / sys_sigprocmask)
    participant Clock as clockintr (CPU 0)
    participant Proc as struct proc
    participant Trap as prepare_return()
    participant Handler as SIGALRM Handler

    App->>Sys: sigprocmask(SIG_BLOCK, SIGALRM)
    Sys->>Proc: blocked_signals |= (1 << SIGALRM)
    
    App->>Sys: alarm(5)
    Sys->>Proc: alarm_deadline = ticks + 5
    
    Note over Clock, Proc: Đồng hồ hệ thống trôi dần từng tick...
    Clock->>Proc: now >= alarm_deadline ?
    Clock->>Proc: alarm_deadline = 0, pending_signals |= (1 << SIGALRM)
    Note over Proc: Tín hiệu bị chặn: nằm chờ trong pending, chưa thể delivery!
    
    App->>Sys: sigprocmask(SIG_UNBLOCK, SIGALRM)
    Sys->>Proc: blocked_signals &= ~(1 << SIGALRM)
    
    Note over Sys, Trap: Syscall kết thúc, tiến trình đi qua prepare_return()
    Trap->>Proc: ready = pending & ~blocked (SIGALRM xuất hiện!)
    Trap->>Handler: Kích hoạt User Handler cho SIGALRM
    opt Tự Re-arm tuần hoàn
        Handler->>Sys: alarm(5) (Cài đặt chu kỳ hẹn giờ tiếp theo)
    end
    Handler->>Trap: sigreturn() quay về luồng chính
```

---

## 7. Ma trận Kiểm thử Module Masking & Alarm

### Bảng Kiểm thử Mặt nạ Tín hiệu (`MK-01` đến `MK-07`)
| Mã Test | Tình huống kiểm thử | Hành động & Dữ liệu đầu vào | Kết quả mong đợi |
|---|---|---|---|
| **MK-01** | Chặn tín hiệu bằng `SIG_BLOCK` | Đăng ký handler `SIGUSR1`, gọi `sigprocmask(SIG_BLOCK, &set, 0)` | `SIGUSR1` bị chặn, gửi signal không kích hoạt handler |
| **MK-02** | Bỏ chặn tín hiệu bằng `SIG_UNBLOCK` | Tiếp nối MK-01, gửi signal, sau đó unblock | Handler được gọi thực thi ngay sau khi `sigprocmask` trả về |
| **MK-03** | Thiết lập toàn bộ mask bằng `SIG_SETMASK` | Gọi `SETMASK` với tập bit mới | `blocked_signals` đổi chính xác theo mask mới |
| **MK-04** | Đọc mask cũ qua tham số `oldset` | Gọi `sigprocmask` với `oldset` hợp lệ | Nhận đúng giá trị mask của trạng thái trước đó |
| **MK-05** | Ngăn chặn việc chặn `SIGKILL` | Cố tình truyền bit `SIGKILL` vào `set` | Bit `SIGKILL` bị `sig_sanitize` loại bỏ, không thể chặn |
| **MK-06** | Tham số `how` không hợp lệ | Truyền `how = 99` | Syscall trả về `-1`, mask không bị biến đổi |
| **MK-07** | Con trỏ `set` hoặc `oldset` rác | Truyền địa chỉ bộ nhớ không hợp lệ | Syscall phát hiện lỗi copy và trả về `-1` |

### Bảng Kiểm thử Hẹn giờ Alarm (`AL-01` đến `AL-07`)
| Mã Test | Tình huống kiểm thử | Hành động & Dữ liệu đầu vào | Kết quả mong đợi |
|---|---|---|---|
| **AL-01** | Hẹn giờ cơ bản `alarm(n)` | Đặt handler `SIGALRM`, gọi `alarm(5)` | Handler được gọi sau khoảng 5 ticks |
| **AL-02** | Hủy hẹn giờ qua `alarm(0)` | Gọi `alarm(50)`, sau đó gọi `alarm(0)` | Hủy thành công, không có `SIGALRM` nào kích hoạt |
| **AL-03** | Ghi đè hẹn giờ cũ | Gọi `alarm(50)`, sau đó gọi `alarm(10)` | Trả về số tick còn lại của lần trước (~50), deadline mới theo 10 |
| **AL-04** | Demo Periodic Timer (tự re-arm) | Trong handler `SIGALRM`, gọi lại `alarm(5)` | Handler được gọi đều đặn theo chu kỳ ~5 ticks |
| **AL-05** | Kết hợp `alarm` và `sigprocmask` | Block `SIGALRM`, gọi `alarm(3)`, chờ 5 ticks | Alarm hết hạn, `SIGALRM` giữ pending; khi unblock thì handler chạy |
| **AL-06** | Độc lập đa CPU với Alarm | Chạy test alarm trên `CPUS=1` và `CPUS=4` | Tốc độ trôi deadline không bị phụ thuộc số lượng CPU |
| **AL-07** | Fork không thừa kế Alarm | Cha gọi `alarm(50)`, gọi `fork()` | Tiến trình con có `alarm_deadline == 0`, không bị kích hoạt nhầm |

---

## 8. Đặc tả Semantics `pause(n)` tương tác với Signals

### 8.1. Hiện trạng Syscall `pause(int n)` trong xv6-riscv
Trong baseline xv6-riscv hiện hành, syscall đưa tiến trình vào trạng thái ngủ theo thời gian được định nghĩa là `int pause(int n)` (số hiệu `SYS_pause 13`), thay thế cho hàm `sleep(n)` cũ của xv6.
- **Vòng lặp chờ:**
  ```c
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
  ```
- **Hành vi khi có Signal:**
  1. Khi nhận tín hiệu có thể phân phối (deliverable: không bị mask trong `blocked_signals` và không đặt `SIG_IGN`), `sig_interrupted()` trả về 1.
  2. `sys_pause()` ngắt vòng lặp ngay lập tức và trả về `-1`.
  3. Giá trị `-1` được nạp vào `p->trapframe->a0`.
  4. Trước khi trở lại user mode, kernel đi qua `prepare_return() -> sig_deliver() -> sig_setup_frame()` để thiết lập frame và thực thi handler.
  5. Khi handler kết thúc bằng `sigreturn()`, giá trị `a0 = -1` được khôi phục nguyên vẹn, người dùng nhận giá trị trả về `-1` biểu thị syscall đã bị ngắt bởi tín hiệu.
- **Hành vi khi Signal bị Block hoặc Ignore:**
  - Nếu signal gửi đến đang nằm trong `blocked_signals`, `sig_interrupted()` trả về 0. `pause` tiếp tục ngủ đủ `n` ticks và trả về 0.
  - Sau khi `pause` kết thúc, nếu user unblock signal đó thì handler mới được kích hoạt.
- **Hỗ trợ mô phỏng POSIX `pause(void)`:**
  - Để chờ vô hạn cho tới khi có tín hiệu đánh thức (chuẩn POSIX `pause()`), chương trình user có thể gọi `pause(0x7fffffff)`. Khi có signal đến, tiến trình thức dậy ngay và trả về `-1`.

### 8.2. Ma trận Kiểm thử `pause()` (`PA-01` đến `PA-06`)
| Mã Test | Tình huống kiểm thử | Thiết lập & Hành động | Kết quả mong đợi |
|---|---|---|---|
| **PA-01** | Gọi `pause(0)` | Gọi `pause(0)` | Trả về `0` ngay lập tức |
| **PA-02** | `SIGALRM` ngắt `pause` dài | Đăng ký handler, đặt `alarm(5)`, gọi `pause(1000)` | Trả về `-1`, handler chạy 1 lần, thức dậy sau < 100 ticks |
| **PA-03** | `SIGALRM` bị block | Chặn `SIGALRM`, `alarm(3)`, gọi `pause(10)` | Trả về `0`, ngủ đủ 10 ticks, unblock xong handler mới chạy |
| **PA-04** | Tín hiệu bị bỏ qua `SIG_IGN` | Đặt `SIGUSR1 = SIG_IGN`, tiến trình con gửi `SIGUSR1` trong lúc cha `pause(15)` | `pause` không bị ngắt, trả về `0` |
| **PA-05** | Tiến trình khác gửi signal ngắt `pause` | Đặt handler `SIGUSR1`, con gửi `SIGUSR1` trong lúc cha `pause(1000)` | `pause` bị ngắt, trả về `-1`, handler chạy 1 lần |
| **PA-06** | Mô phỏng chờ vô hạn | Đặt `alarm(5)`, gọi `pause(0x7fffffff)` | Thức dậy bởi `SIGALRM`, trả về `-1` |

---

## 9. Kiến trúc và Hiện thực Định tuyến Bắt phím `Ctrl-C` $\to$ `SIGINT` (Tuần 4 — PA-1)

### 9.1. Luồng Nhận Ký tự từ Bàn phím trên xv6
```
Bàn phím -> UART Phần cứng -> Ngắt UART -> plic_claim() -> uartintr() -> consoleintr(int c)
```
Trong `kernel/console.c`, hàm `consoleintr(c)` tiếp nhận từng ký tự:
- Đã hỗ trợ: `Ctrl-P` (`procdump`), `Ctrl-U` (xóa dòng), `Ctrl-H` / Delete (xóa ký tự), `Ctrl-D` (EOF).
- **Đã bổ sung ở Tuần 4 (`case C('C')`):** Khi nhận `0x03`, kernel in `^C\n`, xóa phần ký tự chưa nhấn Enter (`cons.e = cons.w`), không đẩy `0x03` vào `cons.buf`, và nếu `fg_pid > 0` thì gọi `kkill(fg_pid, SIGINT)`.

### 9.2. Phân tích Các Phương án Xác định Foreground Process
| Phương án | Cơ chế hoạt động | Ưu điểm | Nhược điểm | Đánh giá |
|---|---|---|---|---|
| **PA-1 (Đề xuất)** | Thêm biến toàn cục `fg_pid` trong kernel (bảo vệ bởi `cons.lock`) + syscall `setfg(int pid)`. Shell `sh.c` gọi `setfg(child_pid)` trước `wait(0)` và `setfg(0)` sau khi lệnh xong. | Đơn giản, an toàn, can thiệp tối thiểu vào xv6 kernel. | Chỉ ngắt được 1 tiến trình con trực tiếp, chưa hỗ trợ chuỗi pipeline `a \| b`. | **Khả thi nhất cho đồ án** |
| **PA-2** | Bổ sung khái niệm Process Group (`p->pgid`) + syscall `setpgid()`. `Ctrl-C` gửi `SIGINT` cho toàn bộ nhóm tiến trình tiền cảnh. | Chuẩn POSIX, hỗ trợ ngắt toàn bộ pipeline. | Phức tạp, phải sửa `allocproc`, `kfork`, vòng đời tiến trình. | Để dành cho giai đoạn Stretch |
| **PA-3** | Quét toàn bộ bảng `proc[]`, gửi `SIGINT` cho mọi tiến trình `pid > 2`. | Không cần sửa `sh.c`. | Giết nhầm cả tiến trình daemon/background (`&`). | Không dùng |

### 9.3. Hiện thực Phương án 1 (PA-1) trên nhánh `feature/ctrlc-sigint`
1. **Trong `kernel/console.c` và `kernel/defs.h`:**
   - Thêm `static int fg_pid = 0;` (bảo vệ bởi `cons.lock`) và `void console_setfg(int pid)`.
   - Thêm nhánh `case C('C'):` trong `consoleintr()`: in `^C\n`, đặt `cons.e = cons.w` để loại bỏ ký tự đang gõ dở, và gọi `kkill(fg_pid, SIGINT)` nếu `fg_pid > 0`.
2. **Syscall `setfg(int pid)` (`SYS_setfg = 27`):**
   - Khai báo trong `kernel/syscall.h`, `kernel/syscall.c`, `kernel/sysproc.c` (`sys_setfg` kiểm tra `pid < 0` trả về `-1`), `user/user.h` và `user/usys.pl`.
3. **Trong `user/sh.c`:**
   ```c
   int pid = fork1();
   if (pid == 0)
     runcmd(parsecmd(cmd));
   setfg(pid);
   wait(0);
   setfg(0);
   ```
   - Lệnh chạy nền (`&`) không bị `Ctrl-C` ngắt nhầm vì child trung gian thoát ngay sau khi fork grandchild.
   - Giới hạn đã biết theo PA-1: chưa hỗ trợ ngắt trực tiếp các tiến trình cháu trong chuỗi pipeline `a | b`.
4. **Quy tắc An toàn và Locking Invariant:**
   - `consoleintr` thực thi trong ngữ cảnh ngắt thiết bị. Hàm `kkill()` chỉ thao tác trên spinlock (`p->lock`), không gọi `sleep()` hay yield CPU, hoàn toàn an toàn khi gọi từ ngắt.
   - Handler của user **tuyệt đối không chạy trong interrupt context**: `kkill()` chỉ bật bit pending và đánh thức tiến trình. Handler sẽ được thực thi khi tiến trình tiền cảnh được lập lịch và quay về user qua `prepare_return()`.
   - Thứ tự khóa tuân thủ: `cons.lock -> p->lock` (nhất quán với `consoleread`).
5. **Trạng thái kiểm thử (`DL-11`):** Đã hoàn tất mã nguồn (`user/ctrlc_demo.c`, `Makefile`), chờ chạy xác minh trên QEMU (`CPUS=1` và `CPUS=4`).
