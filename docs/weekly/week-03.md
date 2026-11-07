# Báo cáo Tiến độ Tuần 3: Đề tài 26106 — POSIX-style Signals trên xv6-riscv

* **Học phần:** Hệ điều hành
* **Đề tài:** 26106 — POSIX-style signals (Processes, ★★)
* **Repository:** [Project-26106---POSIX-style-signals](https://github.com/Quyenlsme/Project-26106---POSIX-style-signals)
* **Nhóm sinh viên:**
  - **Phạm Lê Quyền** (Module: Signal sending & delivery)
  - **Trần Việt Hưng** (Module: Signal handler & context)
  - **Nguyễn Trung Kiên** (Module: Signal masking & timer)
* **Thời gian báo cáo:** Tuần 3 (Từ 01/11/2026 đến 07/11/2026)
* **Baseline xv6-riscv:** commit `47a16bf` trên nhánh `main`

---

## 1. Tổng quan Tiến độ và Kết quả Toàn nhóm trong Tuần 3

Trong Tuần 3, nhóm đã hoàn thành toàn bộ mục tiêu kỹ thuật cốt lõi theo kế hoạch đề ra trong `AGENTS.md`, giải quyết triệt để các vấn đề race condition, tính toàn vẹn của context restoration, semantics của system calls ngắt (`pause`), hoàn thiện bộ kiểm thử tự động, và kiểm tra hồi quy toàn bộ hệ thống trên cả môi trường CPU đơn lõi (`CPUS=1`) lẫn đa lõi (`CPUS=4`).

### 1.1. Bảng phân loại công việc theo trạng thái

| Thành viên | Đã nghiên cứu | Đã thiết kế | Đã code | Đã build | Đã test | Đã merge |
|---|---|---|---|---|---|---|
| **Phạm Lê Quyền** | Race condition lost-wakeup giữa `sleep_prepare` và `sleep`; Invariant lock order `p->lock` và `wait_lock` | Cơ chế `sleep_prepare_intr(void *chan, struct spinlock *lk)` | Thêm `sleep_prepare_intr()` vào `kernel/proc.c`, áp dụng tại 5 điểm sleep; mở rộng `killtest.c` lên 38 checks | Build sạch 100%, 0 warning, 0 error | PASS 38/38 checks trong `killtest` trên QEMU CPUS=1 và CPUS=4 | PR #5 (commit `d8e4b34`) vào `main` |
| **Trần Việt Hưng** | Cơ chế khôi phục thanh ghi CPU RISC-V ABI; Tràn user stack và căn lề 16-byte; Vòng lặp đệ quy cùng signal | Layout `struct sigframe`, kiểm tra an toàn `sp` trong `sys_sigreturn()`, kiểm tra thanh ghi caller-saved `t0-t6`, `a0-a7` | Xây dựng và mở rộng `user/regtest.c` từ REG-01 đến REG-33 (bao gồm HD-01 đến HD-10) | Build sạch 100% với `riscv64-unknown-elf-gcc` | PASS 33/33 tests trong `regtest` trên QEMU CPUS=1 và CPUS=4 | PR #6 (commit `c6ffa5d`) và commit tuần 3 vào `main` |
| **Nguyễn Trung Kiên** | Semantics của `sys_pause` khi nhận signal deliverable; Race condition `p->chan = 0` khi đánh thức process ngủ; Khảo sát console interrupt cho Ctrl+C | Semantics `pause(n)` trả `-1` khi ngắt, giữ pending khi mask; Re-arm pattern cho `alarm()`; Coalescing signal | Khắc phục race `p->chan=0`, sửa vòng lặp `SIG_IGN` trong `sig_deliver`; thêm ca `AL-07`; viết `pausetest.c`, `alarm_demo.c`, `mask_demo.c` | Build sạch 100% | PASS 12/12 checks trong `pausetest`, PASS `sigtest` (kèm AL-07), chạy thành công 2 demo | Commit `7d73a0a`, `9880d92` vào `main` |

---

## 2. Chi tiết Công việc của Từng Thành viên

### 2.1. Phạm Lê Quyền — Module Signal Sending & Delivery

#### Mục tiêu giải quyết
1. Khắc phục triệt để race condition "lost-wakeup" tiềm ẩn trong xv6: nếu signal delivery xảy ra giữa thời điểm tiến trình kiểm tra điều kiện ngủ và thời điểm thực sự giữ `p->lock` để chuyển trạng thái sang `SLEEPING`.
2. Đảm bảo 5 điểm sleep trong kernel (`consoleread`, `pipewrite`, `piperead`, `kwait`, `sys_pause`) không bao giờ bị mất signal đánh thức.
3. Mở rộng `user/killtest.c` bao phủ toàn diện 13 kịch bản kiểm thử delivery (`DL-01` đến `DL-13`).

#### Thay đổi kỹ thuật cụ thể
- **API mới trong Kernel:** Hiện thực hàm `sleep_prepare_intr(void *chan, struct spinlock *lk)` trong [`kernel/proc.c`](file:///home/nguyentrungkien_20233477/Desktop/Project-26106---POSIX-style-signals/kernel/proc.c). Hàm này acquire `p->lock`, kiểm tra `sig_interrupted(p)` trước khi cho phép tiến trình chuyển trạng thái ngủ; nếu đã có signal deliverable pending, tiến trình lập tức hủy việc ngủ và trả mã lỗi `-1` (`EINTR`).
- **File tài liệu kỹ thuật:** Soạn thảo [`docs/signal-core-locking.md`](file:///home/nguyentrungkien_20233477/Desktop/Project-26106---POSIX-style-signals/docs/signal-core-locking.md) phân tích thứ tự giữ khóa (lock ordering) giữa `p->lock`, `wait_lock` và lock của subsystem.
- **Mở rộng kiểm thử:** Bổ sung ca test 500 vòng lặp liên tục gửi signal đúng thời điểm tiến trình con vừa vào `read()` pipe mà không xảy ra bất kỳ lần mất wakeup nào (`killtest` Test 10).

#### Kết quả kiểm thử thực tế
- Lệnh chạy: `killtest` trên terminal QEMU (cả `CPUS=1` và `CPUS=4`).
- Kết quả: **38 PASS / 0 FAIL (ALL PASSED)**.

---

### 2.2. Trần Việt Hưng — Module Signal Handler & Context

#### Mục tiêu giải quyết
1. Hoàn thiện toàn bộ các ca kiểm thử theo kế hoạch cho Handler & Context (`HD-01` đến `HD-10`).
2. Giải quyết lỗ hổng kiểm thử của `REG-02` (vốn chỉ kiểm tra thanh ghi callee-saved `s1-s11`, bị compiler GCC tự động bảo toàn trong hàm C): bổ sung kiểm thử thanh ghi caller-saved `t0-t6` và `a1-a7` bằng inline assembly nguyên khối.
3. Bổ sung ca test `HD-04` (Bảo toàn `a0` không bị mã return của `sigreturn` ghi đè thành 0).
4. Bổ sung ca test `HD-08` (Chống đệ quy vô hạn: tự gửi lại cùng signal trong handler thì signal mới phải pending do tự động mask).
5. Bổ sung ca test `HD-09` (Bad Stack Frame: gọi `sigreturn()` với stack không hợp lệ $\to$ kernel gọi `kexit(-1)`, không gây kernel panic).
6. Bổ sung ca test `HD-10` (Căn lề ngăn xếp 16-byte: `sp % 16 == 0` ngay tại điểm vào handler theo RISC-V Calling Convention).

#### Thay đổi kỹ thuật cụ thể
- Mở rộng [`user/regtest.c`](file:///home/nguyentrungkien_20233477/Desktop/Project-26106---POSIX-style-signals/user/regtest.c) từ 28 lên 33 ca kiểm thử:
  - `REG-29` (`HD-04`): Nạp `a0 = 0x12345678ULL`, chờ ngắt `alarm(2)` bằng inline assembly, handler chạy và `sigreturn`, sau đó đọc lại `a0` xác minh không bị ghi đè.
  - `REG-30` (`HD-08`): Trong handler `SIGUSR1`, gọi `kill(getpid(), SIGUSR1)`; kiểm tra biến đếm để chứng minh handler không bị gọi đệ quy lồng nhau ngay lập tức, mà chỉ kích hoạt lần 2 sau khi lần 1 kết thúc.
  - `REG-31` (`HD-09`): Tiến trình con gọi trực tiếp `SYS_sigreturn` qua `ecall` với `sp = 1` (không căn lề và invalid); kernel phát hiện lỗi, kết thúc tiến trình an toàn bằng `kexit(-1)`, tiến trình cha `wait` nhận status exit lỗi, kernel hoàn toàn không panic.
  - `REG-32` (`HD-10`): Trong handler, dùng inline asm đọc `sp` và kiểm tra `(sp & 0xF) == 0`.
  - `REG-33` (`HD-03` Caller-saved): Nạp các giá trị magic vào `t0-t6`, `a1-a7`, handler `clobber_handler` cố tình ghi đè `0xdead` vào toàn bộ các thanh ghi này; sau `sigreturn`, các thanh ghi caller-saved được khôi phục chính xác 100%.

#### Kết quả kiểm thử thực tế
- Lệnh chạy: `regtest` trên terminal QEMU (cả `CPUS=1` và `CPUS=4`).
- Kết quả: **33 PASS / 0 FAIL (ALL PASSED)**.

---

### 2.3. Nguyễn Trung Kiên — Module Signal Masking & Timer

#### Mục tiêu giải quyết
1. Khắc phục lỗi tiềm ẩn race condition `p->chan = 0` khi đánh thức tiến trình ngủ trong kernel `proc.c`.
2. Khắc phục lỗi lặp vô hạn `sig_deliver` khi signal có action là `SIG_IGN`: phải tiêu thụ bit pending (`p->sig_pending &= ~(1U << sig)`) thay vì bỏ qua.
3. Bổ sung ca test `AL-07`: Kiểm tra tiến trình con tạo ra bởi `fork()` không được thừa kế `alarm_deadline` của tiến trình cha.
4. Hiện thực chương trình kiểm thử [`user/pausetest.c`](file:///home/nguyentrungkien_20233477/Desktop/Project-26106---POSIX-style-signals/user/pausetest.c) gồm 6 ca kiểm thử (`PA-01` đến `PA-06`) cho syscall `pause(n)` khi có signal deliverable.
5. Xây dựng 2 chương trình demo người dùng chuẩn bị cho nghiệm thu:
   - [`user/alarm_demo.c`](file:///home/nguyentrungkien_20233477/Desktop/Project-26106---POSIX-style-signals/user/alarm_demo.c): Demo periodic timer thông qua kỹ thuật re-arm trong handler `SIGALRM` (5 chu kỳ 10 ticks liên tiếp).
   - [`user/mask_demo.c`](file:///home/nguyentrungkien_20233477/Desktop/Project-26106---POSIX-style-signals/user/mask_demo.c): Demo coalescing signal bị chặn và được delivery ngay khi unblock.
6. Khảo sát kỹ thuật cơ chế bắt tổ hợp phím `Ctrl+C` từ console uart để sẵn sàng hiện thực `SIGINT` trong Tuần 4.

#### Kết quả kiểm thử thực tế
- Lệnh chạy: `sigtest`, `pausetest`, `alarm_demo`, `mask_demo` trên QEMU.
- Kết quả:
  - `sigtest`: **ALL TESTS PASSED** (bao gồm `AL-07`).
  - `pausetest`: **12 PASS / 0 FAIL (ALL PASSED)**.
  - `alarm_demo`: Hoàn thành xuất sắc 5 chu kỳ định kỳ 10 ticks.
  - `mask_demo`: 3 signal gửi khi mask được gộp thành 1 lần kích hoạt sau khi unblock.

---

## 3. Nhật ký Kiểm thử Thực tế trên QEMU (Log đối chứng)

### 3.1. Chạy tích hợp các bộ test Signal Core trên QEMU
```text
xv6 kernel is booting
hart 2 starting
hart 1 starting
init: starting sh

$ sigtest
========================================
  SIGNAL SYSTEM INTEGRATION TEST (WEEK 3)
========================================

=== 1. Test sigprocmask ===
  [PASS] SIG_BLOCK SIGUSR1 (ret=0, oldmask=0x0)
  [PASS] Query current mask with set=NULL (mask=0x400)
  [PASS] Invariant: SIGKILL cannot be blocked (mask=0x400)
  [PASS] SIG_UNBLOCK SIGUSR1 (ret=0, cur_mask=0x0)
  [PASS] Invalid how=99 rejected with -1
  => sigprocmask tests: ALL PASSED

=== 2. Test alarm ===
  [PASS] First alarm(10) returned 0
  [PASS] Replaced alarm(25), remaining ticks returned: 10
  [PASS] Canceled alarm(0), remaining ticks returned: 25
  [PASS] Verified clean state after cancel
  [PASS] Negative ticks rejected with -1
  [PASS] AL-07: child does not inherit alarm deadline after fork
  => alarm tests: ALL PASSED

=== 3. Test sigaction ===
  [PASS] sigaction(SIGUSR1, SIG_IGN) succeeded
  [PASS] Invariant: SIGKILL cannot be modified
  [PASS] Invalid signal numbers rejected
  [PASS] Registered custom_handler and retrieved oldact correctly
  => sigaction tests: ALL PASSED

=== 4. Test kill ===
  [PASS] Invalid signal numbers rejected
  [PASS] Non-existent PID rejected
  => kill tests: ALL PASSED

=== 5. Test Mask -> Pending -> Unmask Delivery ===
  -> Sending SIGUSR1 to self while BLOCKED...
  [PASS] Signal remained pending, handler not called yet
  -> Unblocking SIGUSR1...
  -> [Handler] Received signal 10 (count=1)
  [PASS] Pending signal delivered immediately upon unblock!
  => Mask & Delivery integration: ALL PASSED

========================================
  ALL TESTS PASSED SUCCESSFULLY!
========================================

$ pausetest
=== PAUSETEST - pause + signal ===
PA-01: pause(0)
  [PASS] pause(0) == 0
PA-02: alarm(5) ngat pause(1000)
  [PASS] pause tra ve -1 khi bi signal ngat
  [PASS] handler SIGALRM chay dung 1 lan
  [PASS] thuc day som (khong ngu het 1000 ticks)
PA-03: SIGALRM bi block khong ngat pause
  [PASS] pause(10) tra ve 0 (signal bi block)
  [PASS] ngu du 10 ticks
  [PASS] handler chua chay khi dang block
  [PASS] handler chay ngay sau unblock
PA-04: SIG_IGN khong ngat pause
  [PASS] pause(15) tra ve 0 (signal bi ignore)
PA-05: kill tu tien trinh khac ngat pause
  [PASS] pause tra ve -1
  [PASS] handler SIGUSR1 chay dung 1 lan
PA-06: pause(0x7fffffff) cho toi khi co signal
  [PASS] thuc day boi SIGALRM, tra ve -1
=== Ket qua: 12 PASS, 0 FAIL ===
pausetest: ALL PASSED

$ killtest
=== KILLTEST - signal core ===
Test 1: sig khong hop le -> PASS (4 checks)
Test 2: pid khong hop le -> PASS (3 checks)
Test 3: SIGKILL tien trinh dang chay -> PASS (3 checks)
Test 4: SIGKILL tien trinh dang ngu -> PASS (3 checks)
Test 5: SIGUSR1 (SIG_DFL) ket thuc tien trinh -> PASS (3 checks)
Test 6: SIGTERM danh thuc va ket thuc tien trinh dang ngu -> PASS (3 checks)
Test 7 (DL-04): gop signal lap lai khi dang bi chan -> PASS (6 checks)
Test 8 (DL-05): signal so hieu nho duoc giao truoc -> PASS (2 checks)
Test 9 (DL-09): SIG_IGN khong ket thuc tien trinh -> PASS (2 checks)
Test 10 (DL-10): signal co handler ngat read() dang cho -> PASS (3 checks, 500 loops 0 lost wakeups)
Test 11: stress da CPU (4 tien trinh x 200 signal) -> PASS (5 checks)
Test 12 (DL-12): signal bi chan khong ngat read() dang cho -> PASS (1 check)
=== Ket qua: 38 PASS, 0 FAIL ===
killtest: ALL PASSED

$ regtest
===== SIGNAL REGISTER TEST =====
REG-01 PASS: Signal handler was called
REG-02 PASS: s1-s11 preserved after signal
REG-03 PASS: stack pointer restored after sigreturn
REG-04 PASS: blocked signal delivered after unblock
REG-05 PASS: signal mask restored after handler
REG-06 PASS: nested signal handler executed correctly
REG-07 PASS: SIG_IGN ignored the signal
REG-08 PASS: SIG_DFL terminated child process
REG-09 PASS: SIGKILL cannot be blocked
REG-10 PASS: sigprocmask handles NULL arguments
REG-11 PASS: sigaction filters unsupported signals
REG-12 PASS: sigaction rejects invalid signals
REG-13 PASS: sigaction rejects unsupported flags
REG-14 PASS: sigaction rejects handler without restorer
REG-15 PASS: sigaction returns old action
REG-16 PASS: sigaction rejects invalid act address
REG-17 PASS: sigaction rejects invalid oldact address
REG-18 PASS: sigprocmask rejects invalid how
REG-19 PASS: NULL set does not change signal mask
REG-20 PASS: SIGKILL cannot be blocked using SIG_SETMASK
REG-21 PASS: SIG_UNBLOCK only unblocks requested signal
REG-22 PASS: sigaction updates action and returns old action
REG-23 PASS: SIG_IGN does not require sa_restorer
REG-24 PASS: Multiple pending signals were delivered
REG-25 PASS: Child inherited signal handler after fork
REG-26 PASS: Child signal action change did not affect parent
REG-27 PASS: Pending signal was not inherited after fork
REG-28 PASS: Signal actions and mask handled correctly after exec
REG-29 PASS: a0 preserved across sigreturn (HD-04)
REG-30 PASS: Recursive re-entry prevented, pending delivered after sigreturn (HD-08)
REG-31 PASS: Bad stack frame killed process safely without panic (HD-09)
REG-32 PASS: Stack pointer is 16-byte aligned (sp=0x6CD0) (HD-10)
REG-33 PASS: Caller-saved registers t0-t6, a1-a7 preserved across sigreturn (HD-03)
===== TEST COMPLETED =====
```

### 3.2. Kiểm thử Hồi quy Toàn diện `usertests` (xv6 Baseline Regression)
- **Cấu hình Đơn lõi (`CPUS=1`):**
  - Thời gian chạy: ~2 phút 30 giây.
  - Trạng thái: Chạy trọn vẹn toàn bộ các ca kiểm thử file system, paging, fork, pipe, sbrk, exec, trap,...
  - Kết quả cuối cùng: `ALL TESTS PASSED` (đã lưu log chi tiết tại `usertests_cpus1.log`).
- **Cấu hình Đa lõi (`CPUS=4`):**
  - Thời gian chạy: ~4 phút.
  - Trạng thái: Kiểm thử tương tranh bộ nhớ và tiến trình trên 4 CPU đồng thời.
  - Kết quả cuối cùng: `ALL TESTS PASSED` (đã lưu log chi tiết tại `usertests_cpus4.log`).

---

## 4. Tác động Thay đổi và Đánh giá Rủi ro

1. **Về ABI và System Call Interface:**
   - Hoàn toàn giữ nguyên đúng chuẩn đã chốt giữa 3 thành viên trong các tuần trước.
   - Các syscall `sigaction`, `sigprocmask`, `sigreturn`, `alarm`, `kill` hoạt động nhất quán, tương thích với cả user C thông thường lẫn inline assembly.
2. **Về Tính ổn định của Kernel xv6:**
   - Việc bổ sung `sleep_prepare_intr()` và kiểm tra signal deliverable tại các điểm ngủ không làm thay đổi ngữ nghĩa gốc của xv6 đối với các tiến trình không sử dụng signal.
   - `usertests` pass 100% trên cả 1 CPU và 4 CPU chứng minh hạt nhân xv6 vẫn duy trì tính toàn vẹn tuyệt đối.

---

## 5. Kế hoạch Tuần 4

Theo lộ trình 8 tuần trong `AGENTS.md`, các mục tiêu ưu tiên cho Tuần 4 gồm:
1. **Phạm Lê Quyền:** Khảo sát và hiện thực cơ chế bắt tổ hợp phím `Ctrl+C` từ UART/console driver để gửi tín hiệu `SIGINT` tới nhóm tiến trình foreground (hoàn thiện Demo 1).
2. **Trần Việt Hưng:** Rà soát và tối ưu hóa nested signal handlers trong điều kiện nhiều tín hiệu dồn dập (stress nested depth).
3. **Nguyễn Trung Kiên:** Tích hợp kiểm thử hoàn chỉnh cho kịch bản Demo 1 (`Ctrl+C`), Demo 2 (`alarm` periodic), Demo 3 (`mask/unmask`); chuẩn bị kịch bản quay video nghiệm thu.
