# Thiết kế Kỹ thuật Tổng thể: POSIX-style Signals trên xv6-riscv

* **Đề tài môn học:** Hệ điều hành — Đề tài 26106 (Processes, ★★)
* **Repository:** [Project-26106---POSIX-style-signals](https://github.com/Quyenlsme/Project-26106---POSIX-style-signals)
* **Thành viên:** Phạm Lê Quyền, Trần Việt Hưng, Nguyễn Trung Kiên
* **Vai trò tài liệu:** **Single Source of Truth (Nguồn chân lý duy nhất)** quy định kiến trúc tổng thể, quy ước ABI/API, phân chia module, cấu trúc dữ liệu, mô hình đồng bộ và các quyết định thiết kế chung (D1 đến D6).

---

## 1. Phân chia Trách nhiệm & Ranh giới Module

Dự án được phân rã thành 3 module kỹ thuật độc lập tương ứng với 3 thành viên, phối hợp chặt chẽ qua các giao diện kernel chuẩn hóa:

```
┌────────────────────────────────────────────────────────────────────────┐
│                      HỆ THỐNG POSIX SIGNALS TRÊN XV6                   │
├─────────────────────┬───────────────────────────┬──────────────────────┤
│ PHẠM LÊ QUYỀN       │ TRẦN VIỆT HƯNG            │ NGUYỄN TRUNG KIÊN    │
│ (Sending & Delivery)│ (Handler & Context)       │ (Masking & Timer)    │
├─────────────────────┼───────────────────────────┼──────────────────────┤
│ • kkill(pid, sig)   │ • ksigaction()            │ • ksigprocmask()     │
│ • p->pending_signals│ • struct sigaction        │ • p->blocked_signals │
│ • sig_send()        │ • struct sigframe         │ • kalarm()           │
│ • sig_deliver()     │ • sig_setup_frame()       │ • p->alarm_deadline  │
│                     │ • ksigreturn()            │ • sig_alarm_check()  │
│                     │ • Bảo toàn trapframe & a0 │ • sig_sanitize()     │
│                     │                           │ • Bắt phím Ctrl+C    │
└─────────────────────┴───────────────────────────┴──────────────────────┘
```

- **Quyền $\leftrightarrow$ Kiên:** Thống nhất định dạng bitmask 32-bit (`sigset_t`). Kiên quản lý mặt nạ `blocked_signals`, Quyền sử dụng `pending & ~blocked` để chọn tín hiệu cần delivery. Timer của Kiên hết hạn sẽ enqueue `SIGALRM` vào `pending_signals` của Quyền.
- **Quyền $\leftrightarrow$ Hưng:** Khi Quyền chọn được tín hiệu có đăng ký hàm xử lý, Quyền gọi `sig_setup_frame()` của Hưng tại `prepare_return()`. Hưng chuẩn bị stack frame và cập nhật trapframe để CPU nhảy sang handler của người dùng.
- **Hưng $\leftrightarrow$ Kiên:** Khi vào handler, Hưng tự động cập nhật mặt nạ mới (`blocked_signals |= act->sa_mask | (1 << sig)`). Khi gọi `sigreturn()`, Hưng khôi phục lại mặt nạ cũ `saved_mask`.

---

## 2. Các Quyết định Thiết kế Cốt lõi (D1 – D6)

Tất cả các thành viên và module bắt buộc phải tuân thủ tuyệt đối 6 quyết định thiết kế sau:

### D1: Gửi Tín hiệu & Chữ ký `kill()`
- Phía user: `int kill(int pid, int sig);`. Phía kernel: `int kkill(int pid, int sig);`.
- Nếu `sig == SIGKILL` (9): Giữ nguyên đường xử lý gốc của xv6 (`p->killed = 1`) và đánh thức tiến trình ngủ để diệt dứt điểm.
- Với các tín hiệu khác: Kernel ghi nhận sự kiện vào bitmask `p->pending_signals |= (1U << sig)`.

### D2: Quản lý Mặt nạ Tín hiệu `sigprocmask()`
- Chữ ký syscall: `int sigprocmask(int how, const sigset_t *set, sigset_t *oldset);`.
- Phía `user.h` sử dụng kiểu `uint` tương đương `sigset_t`.
- Tham số `set` và `oldset` truyền qua con trỏ; `oldset` có thể bằng 0 (NULL) nếu không cần đọc lại mask cũ.

### D3: Cấu trúc `sigaction` và `sigframe`
- `struct sigaction` bao gồm:
  - `sa_handler`: Con trỏ hàm xử lý, hoặc `SIG_DFL`, hoặc `SIG_IGN`.
  - `sa_mask`: Mặt nạ các tín hiệu bị chặn bổ sung trong lúc handler thực thi.
  - `sa_flags`: Cờ điều khiển (hiện tại bắt buộc phải bằng 0).
  - `sa_restorer`: Địa chỉ hàm trampoline ở user space thực hiện `ecall` gọi `sigreturn()`.
- `struct sigframe` chỉ bao gồm:
  - `saved_tf`: Lưu lại toàn bộ 32 thanh ghi CPU cũ.
  - `saved_mask`: Lưu lại mặt nạ tín hiệu cũ.
  *(Không lưu `signum` và `ret_addr` trong struct vì chúng được nạp trực tiếp vào thanh ghi phần cứng `a0` và `ra` theo chuẩn RISC-V ABI)*.

### D4: Quy ước Đặt tên Hàm
- Hàm bọc syscall cấp cao dùng tiền tố `k`: `kkill`, `ksigaction`, `ksigprocmask`, `ksigreturn`, `kalarm`.
- Hàm tiện ích nội bộ trong kernel dùng tiền tố `sig_`: `sig_send`, `sig_deliver`, `sig_sanitize`, `sig_setup_frame`, `sig_alarm_check`.
- Tên các trường trong `struct proc` giữ nguyên định dạng thống nhất: `pending_signals`, `blocked_signals`, `sig_actions`, `alarm_deadline`.

### D5: Hành vi Mặc định (Default Action)
- Đối với các tín hiệu được hỗ trợ trong đề tài, nếu người dùng không đăng ký handler (`SIG_DFL`), kernel sẽ kết thúc tiến trình sạch sẽ qua `kexit(-1)`.
- Chế độ bỏ qua `SIG_IGN` được hỗ trợ cho mọi tín hiệu, **ngoại trừ `SIGKILL`**.

### D6: Vòng đời Tiến trình & Quy tắc Phân nhóm Khóa
- **Thao tác `kfork()`:** Tiến trình con **thừa kế** `blocked_signals` và bảng `sig_actions` từ cha; nhưng **không thừa kế** `pending_signals` (bằng 0) và `alarm_deadline` (bằng 0).
- **Thao tác `kexec()`:** Tất cả các handler người dùng cài đặt bị reset về `SIG_DFL` (nhưng giữ nguyên `SIG_IGN`), giữ nguyên `blocked_signals` và `pending_signals`, hủy bộ hẹn giờ (`alarm_deadline = 0`).
- **Phân nhóm Khóa (Locking Invariant):**
  - Nhóm cần `p->lock`: `pending_signals` và `alarm_deadline` (vì có thể bị ghi bởi CPU khác hoặc timer interrupt).
  - Nhóm dữ liệu riêng (Private to process): `blocked_signals` và `sig_actions[NSIG]` (chỉ tiến trình hiện hành tự thao tác, không cần giữ `p->lock`).

---

## 3. Kiến trúc Bộ nhớ User Stack & Calling Convention (RISC-V)

Để kích hoạt handler một cách an toàn và khôi phục nguyên vẹn 100% ngữ cảnh:

```
Địa chỉ cao
       │
       │   ┌──────────────────────────────────────────────┐
       │   │  Ngăn xếp Chương trình chính (Main Program) │
       │   └──────────────────────────────────────────────┘
       │                          ▲
       │                          │  (Con trỏ sp ban đầu)
       │   ┌──────────────────────┴───────────────────────┐
       │   │  struct sigframe                             │
       │   │    - saved_tf: Chụp 32 thanh ghi CPU         │
       │   │    - saved_mask: Mặt nạ cũ của tiến trình    │
       │   └──────────────────────────────────────────────┘
       │                          ▲
       │                          │  new_sp = (sp_cũ - sizeof(sigframe)) & ~0xF
       │   ┌──────────────────────┴───────────────────────┐
       │   │  Vùng nhớ thực thi cho hàm Signal Handler    │
       │   └──────────────────────────────────────────────┘
       ▼
Địa chỉ thấp
```

### Quy tắc Cài đặt Thanh ghi trước khi vào Handler:
1. `trapframe->a0 = signum`: Truyền số hiệu tín hiệu làm tham số đầu tiên cho hàm `void handler(int sig)`.
2. `trapframe->ra = (uint64)act->sa_restorer`: Khi handler kết thúc bằng lệnh `ret`, CPU tự động nhảy tới restorer để gọi `sigreturn()`.
3. `trapframe->epc = (uint64)act->sa_handler`: Điểm bắt đầu thực thi của hàm handler.
4. `trapframe->sp = new_sp`: Con trỏ ngăn xếp mới đã được trừ kích thước `sigframe` và ép căn lề 16-byte (`sp & ~0xF`).

### Quy tắc Khôi phục tại `sigreturn()`:
1. Xác thực địa chỉ frame: Phải căn lề 16-byte và nằm hoàn toàn trong vùng nhớ user hợp lệ `[0, p->sz)`. Nếu vi phạm, gọi `kexit(-1)` ngay lập tức.
2. Khôi phục lại toàn bộ 32 thanh ghi từ `saved_tf`.
3. Giữ nguyên các trường cấu hình bất biến của kernel: `kernel_satp`, `kernel_sp`, `kernel_trap`, `kernel_hartid`.
4. **Bảo toàn thanh ghi `a0`:** Trong `kernel/syscall.c`, dispatcher phải bỏ qua việc gán `p->trapframe->a0 = syscalls[num]()` cho `SYS_sigreturn`.

---

## 4. Mô hình Timer & Deadlock-Free Lock Ordering

### 4.1. Ngắt Đồng hồ và Deadline Tuyệt đối
Kernel sử dụng biến toàn cục `ticks` làm thước đo thời gian chuẩn:
- Khi gọi `alarm(n)`: Kernel tính `deadline = ticks + n`.
- Tốc độ trôi deadline hoàn toàn độc lập với việc tiến trình đang chạy hay đang ngủ, và độc lập 100% với số lượng CPU (`CPUS=1` hay `CPUS=4`).
- So sánh hết hạn trong ngắt timer CPU 0 (`clockintr`):
  ```c
  if ((int64)(now - p->alarm_deadline) >= 0) { ... }
  ```
  Phép trừ ép kiểu có dấu đảm bảo an toàn tuyệt đối ngay cả khi biến `ticks` bị tràn số (integer overflow).

### 4.2. Thứ tự Khóa Bất biến (Lock Ordering)
Quy tắc toàn cục trong toàn bộ codebase:
$$\text{tickslock} \longrightarrow p\text{->lock}$$

Trong hàm `kalarm()`:
1. Lấy `tickslock`, đọc biến `ticks`, nhả `tickslock`.
2. Lấy `p->lock`, cập nhật `alarm_deadline`, nhả `p->lock`.
Tuyệt đối không lồng hai khóa này vào nhau để triệt tiêu hoàn toàn nguy cơ deadlock.

---

## 5. Danh mục System Call Mới trong Hệ thống

| Tên Syscall | Số hiệu (`syscall.h`) | Prototype User (`user.h`) | Prototype Kernel (`defs.h`) |
|---|---|---|---|
| `sigaction` | `23` | `int sigaction(int, const struct sigaction *, struct sigaction *);` | `int ksigaction(int, uint64, uint64);` |
| `sigreturn` | `24` | `int sigreturn(void);` | `uint64 ksigreturn(void);` |
| `sigprocmask` | `25` | `int sigprocmask(int, const unsigned int *, unsigned int *);` | `int ksigprocmask(int, uint64, uint64);` |
| `alarm` | `26` | `int alarm(int);` | `int kalarm(int);` |
| `setfg` | `27` | `int setfg(int);` | `void console_setfg(int);` |
| `kill` | `6` (sửa đổi) | `int kill(int, int);` | `int kkill(int, int);` |

---

## 6. Quy tắc Signal của Toàn bộ Hệ thống (System Signal Invariants)

Nhóm thống nhất 8 bất biến kỹ thuật cốt lõi chi phối toàn bộ hành vi xử lý tín hiệu trên xv6-riscv, mỗi quy tắc đều được chứng minh bằng các bài kiểm thử thực tế:

1. **Gộp tín hiệu lặp lại (Signal Coalescing):**
   - Trạng thái tín hiệu chờ được lưu dưới dạng bitmask `p->pending_signals`.
   - Nếu cùng một tín hiệu được gửi nhiều lần liên tiếp khi tiến trình chưa kịp xử lý hoặc đang bị chặn, bitmask chỉ giữ một bit `1`.
   - Khi unblock, handler chỉ thực thi đúng 1 lần duy nhất.
   - *Chứng minh:* Kiểm thử `DL-04`, `killtest` Test 7.

2. **Thứ tự ưu tiên giao tín hiệu (Deterministic Signal Priority):**
   - Khi có nhiều tín hiệu đủ điều kiện giao cùng lúc (`ready = pending & ~blocked`), kernel luôn chọn tín hiệu có số hiệu nhỏ nhất trước (duyệt từ bit 1 đến bit 31).
   - *Chứng minh:* Kiểm thử `DL-05` (`killtest` Test 8), `HD-13` (`regtest` REG-36).

3. **`SIGKILL` là tối cao — Không thể chặn hoặc bắt (Unblockable & Uncatchable):**
   - `sigprocmask()` tự động lọc bỏ bit `SIGKILL` khỏi tập bị chặn qua `sig_sanitize()`.
   - `sigaction()` từ chối đăng ký handler hoặc ignore cho `SIGKILL` (trả về `-1`).
   - `SIGKILL` đánh thức tiến trình và kết thúc tiến trình ngay cả khi đang ngủ trong `pause()`.
   - *Chứng minh:* `DL-15` (`killtest` Test 14), `MK-05` (`regtest` REG-09, REG-20), `HD-02` (`regtest` REG-11, REG-12).

4. **Tự động chặn và `sa_mask` trong khi chạy Handler:**
   - Khi CPU nhảy vào handler của tín hiệu `sig`, kernel tự động thêm chính `sig` và toàn bộ mặt nạ `act->sa_mask` vào `blocked_signals`.
   - Ngăn chặn đệ quy vô hạn làm sập stack, đồng thời hoãn các tín hiệu trong `sa_mask` cho tới khi handler kết thúc.
   - Khi gọi `sigreturn()`, mask cũ được khôi phục chính xác về trạng thái trước khi nhận ngắt.
   - *Chứng minh:* `HD-06` (REG-05), `HD-08` (REG-30), `HD-14` (REG-37), `HD-15` (REG-38).

5. **Ngắt System Call và trả về `-1`:**
   - Khi tiến trình đang ngủ chờ trong các syscall có thể ngắt (`consoleread`, `piperead`, `pipewrite`, `kwait`, `sys_pause`), sự xuất hiện của tín hiệu có handler sẽ đánh thức tiến trình qua `sleep_prepare_intr()`, xóa `p->chan`, và syscall trả về `-1`.
   - Tín hiệu được giao ngay ở lần trở về user mode của chính syscall đó.
   - *Chứng minh:* `DL-10` (`killtest` T4, T6, T10), `PA-02`, `PA-05`.

6. **Kế thừa trạng thái qua `fork()` và `exec()`:**
   - Qua `fork()`: tiến trình con thừa kế toàn bộ cấu hình `sig_actions[]` và `blocked_signals` của cha; `pending_signals` và `alarm_deadline` được reset về 0 (con không thừa kế timer của cha).
   - Qua `exec()`: toàn bộ handler tùy biến được reset về `SIG_DFL` (vì mã hàm của handler cũ không còn trong không gian địa chỉ mới); `blocked_signals` được giữ nguyên.
   - *Chứng minh:* `IN-03` (REG-25, REG-26, REG-27), `IN-04` (REG-28), `AL-07` (`sigtest` T2.6).

7. **Hành vi mặc định (`SIG_DFL`):**
   - Mọi tín hiệu được hỗ trợ (`SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGUSR1`, `SIGUSR2`, `SIGALRM`, `SIGTERM`) nếu không đăng ký handler thì hành vi mặc định là kết thúc tiến trình với exit code `-1`.
   - *Chứng minh:* `DL-08`, `DL-18`.

8. **Định tuyến Ngắt Thiết bị (Ctrl-C $\to$ `SIGINT`):**
   - Xử lý hoàn toàn trong ngữ cảnh ngắt UART (`consoleintr`), tuân thủ nghiêm ngặt thứ tự khóa `cons.lock -> p->lock`.
   - Kernel chỉ chuyển tín hiệu cho tiến trình tiền cảnh (`fg_pid > 0`) do shell đăng ký qua syscall `setfg(pid)`.
   - Tuyệt đối không chạy user handler trong interrupt context; handler chỉ được thực thi khi tiến trình tiền cảnh quay về user mode.
   - *Chứng minh:* `DL-11`, `AU-03`.

