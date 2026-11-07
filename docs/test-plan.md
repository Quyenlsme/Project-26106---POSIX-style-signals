# Kế hoạch Kiểm thử Tổng thể: POSIX-style Signals trên xv6-riscv

* **Đề tài môn học:** Hệ điều hành — Đề tài 26106 (Processes, ★★)
* **Repository:** [Project-26106---POSIX-style-signals](https://github.com/Quyenlsme/Project-26106---POSIX-style-signals)
* **Thành viên:** Phạm Lê Quyền, Trần Việt Hưng, Nguyễn Trung Kiên
* **Mục tiêu:** Cung cấp ma trận kiểm thử toàn diện, quy trình thiết lập môi trường, và kịch bản nghiệm thu 3 yêu cầu demo cốt lõi của đề tài.

---

## 1. Môi trường và Quy tắc Kiểm thử

### 1.1. Cấu hình Môi trường
- **Môi trường mô phỏng:** QEMU emulator version 7.0+ trên kiến trúc `riscv64`.
- **Toolchain biên dịch:** `riscv64-unknown-elf-gcc` (hoặc `riscv64-linux-gnu-gcc`).
- **Cấu hình CPU:**
  - Cấu hình đơn lõi: `make qemu CPUS=1` (kiểm tra tính đúng đắn logic).
  - Cấu hình đa lõi: `make qemu CPUS=4` (kiểm tra tương tranh, đồng bộ hóa khóa và timer độc lập).

### 1.2. Tiêu chuẩn Báo cáo Kết quả (Strict PASS/FAIL)
Theo quy định tại `AGENTS.md`, một ca kiểm thử chỉ được đánh dấu là **PASS** khi:
1. Đã được chạy thực tế trên QEMU (không công nhận nếu chỉ mới biên dịch thành công).
2. Có nhật ký đầu ra (log thực tế) khớp 100% với kết quả mong đợi.
3. Không gây ra lỗi rò rỉ bộ nhớ, treo hệ thống hoặc kernel panic.
4. Định dạng báo cáo bắt buộc: **Mã Test $\to$ Thiết lập (Setup) $\to$ Hành động (Action) $\to$ Kỳ vọng (Expected) $\to$ Thực tế (Actual) $\to$ Kết luận (PASS/FAIL)**.

---

## 2. Ma trận Kiểm thử Chi tiết theo Module

### 2.1. Module Delivery & Sending — Phạm Lê Quyền (`DL-01` đến `DL-13`)

| Mã Test | Tình huống kiểm thử | Thiết lập & Hành động | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| **DL-01** | Gửi signal hợp lệ có handler | Đăng ký handler `SIGUSR1`, gọi `kill(pid, SIGUSR1)` | Handler thực thi thành công, tiến trình tiếp tục chạy | PASS — `killtest` T10 |
| **DL-02** | Signal bị blocked | Chặn `SIGUSR1`, gửi `SIGUSR1` | Handler chưa chạy; bit tồn tại trong `pending_signals` | PASS — `killtest` T7 |
| **DL-03** | Delivery sau khi unblock | Tiếp nối DL-02, gọi `sigprocmask(SIG_UNBLOCK)` | Handler kích hoạt ngay sau khi unblock | PASS — `killtest` T7 |
| **DL-04** | Gộp signal lặp lại | Chặn `SIGUSR1`, gửi 5 lần liên tiếp (từ chính mình và từ tiến trình khác), mở chặn | Handler chỉ chạy đúng 1 lần duy nhất | PASS — `killtest` T7 |
| **DL-05** | Thứ tự ưu tiên tín hiệu | Chặn cả hai, gửi `SIGUSR2` (12) rồi `SIGUSR1` (10), mở chặn cùng lúc | `SIGUSR1` (số hiệu nhỏ hơn) được delivery trước | PASS — `killtest` T8 |
| **DL-06** | Gửi tới PID không tồn tại | Gọi `kill(99999, SIGUSR1)`, `kill(0, …)`, `kill(-5, …)` | Syscall trả về `-1`, không ảnh hưởng kernel | PASS — `killtest` T2 |
| **DL-07** | Số hiệu signal không hợp lệ | Gọi `kill(pid, 0)`, `kill(pid, -1)`, `kill(pid, 32)`, `kill(pid, 1000)` | Syscall trả về `-1` | PASS — `killtest` T1 |
| **DL-08** | Hành vi mặc định `SIG_DFL` | Không đặt handler, gửi `SIGUSR1` / `SIGTERM` | Tiến trình kết thúc ngay lập tức với exit code -1 | PASS — `killtest` T5, T6 |
| **DL-09** | Bỏ qua tín hiệu `SIG_IGN` | Đặt `SIG_IGN` cho `SIGUSR1`, gửi cho chính mình và gửi khi đang chờ `read()` | Tín hiệu bị tiêu thụ, tiến trình không bị ngắt, `read()` vẫn nhận dữ liệu | PASS — `killtest` T9 |
| **DL-10** | Đánh thức tiến trình SLEEPING | Con chờ trên `read()` pipe, cha gửi `SIGKILL` / `SIGTERM` / `SIGUSR1` (có handler); lặp 500 vòng gửi đúng lúc con vừa vào `read()` | Con được đánh thức; `SIG_DFL` → thoát `-1`; có handler → `read()` trả `-1`, handler chạy, con chạy tiếp; 0 lần mất wakeup | PASS — `killtest` T4, T6, T10 |
| **DL-11** | Bắt ngắt Ctrl+C | Chương trình chạy lặp vô tận, nhấn Ctrl+C | `SIGINT` kích hoạt handler thành công | Chưa chạy — Kiên khảo sát Ctrl-C tuần 3, hiện thực tuần 4 |
| **DL-12** | Signal bị chặn không ngắt syscall | Chặn `SIGUSR2`, tiến trình đang chờ `read()`, gửi `SIGUSR2` rồi ghi dữ liệu vào pipe | `read()` không bị ngắt, vẫn nhận đúng dữ liệu | PASS — `killtest` T12 |
| **DL-13** | Stress gửi signal đồng thời đa CPU | `CPUS=4`: 4 tiến trình × 200 `kill()` đồng thời vào 1 nạn nhân đang chặn; 4 tiến trình cùng `SIGKILL` 4 nạn nhân | Không mất bit pending (mỗi loại chạy handler đúng 1 lần), không panic / deadlock | PASS — `killtest` T11 |

---

### 2.2. Module Handler & Context — Trần Việt Hưng (`HD-01` đến `HD-10`)

| Mã Test | Tình huống kiểm thử | Thiết lập & Hành động | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| **HD-01** | Đăng ký Handler hợp lệ | Gọi `sigaction` cài đặt handler cho `SIGUSR1`, đọc `oldact` | Đăng ký thành công, `oldact` nhận lại cấu hình cũ | Chưa chạy |
| **HD-02** | Từ chối can thiệp `SIGKILL` | Gọi `sigaction(SIGKILL, &act, 0)` | Trả về `-1`, handler của `SIGKILL` bất biến | Chưa chạy |
| **HD-03** | Bảo toàn 100% thanh ghi CPU | Nạp các số nguyên cụ thể vào `s0-s11`, `a1-a7`, nhận signal | Sau khi handler xong, tất cả thanh ghi giữ nguyên giá trị | Chưa chạy |
| **HD-04** | Bảo toàn thanh ghi `a0` | Chương trình chính đang giữ giá trị `a0 = 0x1234` | Sau `sigreturn()`, `a0` vẫn giữ nguyên `0x1234` | Chưa chạy |
| **HD-05** | Khôi phục Program Counter | Đang thực hiện vòng lặp tính toán thì bị ngắt | Handler kết thúc, vòng lặp chạy tiếp đúng vị trí bị dừng | Chưa chạy |
| **HD-06** | Khôi phục Signal Mask | Trước handler mask = M1; trong handler mask = M2 | Sau `sigreturn()`, mask được khôi phục chính xác về M1 | Chưa chạy |
| **HD-07** | Nested Handlers 2 tầng | Handler `SIGUSR1` nhận tiếp `SIGUSR2` | Handler 2 chạy và unwind trước, sau đó Handler 1 hoàn tất | Chưa chạy |
| **HD-08** | Chống đệ quy lặp cùng signal | Trong handler `SIGUSR1`, gửi lại chính `SIGUSR1` | Signal mới bị pending, không bị lồng đệ quy tràn stack | Chưa chạy |
| **HD-09** | Xử lý Bad Stack Frame | Gọi `sigreturn()` với con trỏ stack không căn lề hoặc trỏ ra ngoài | Kernel gọi `kexit(-1)` diệt tiến trình an toàn, không panic | Chưa chạy |
| **HD-10** | Căn lề Stack 16-byte | Đọc giá trị thanh ghi `sp` ngay đầu hàm handler | `sp % 16 == 0` tuyệt đối theo RISC-V ABI | Chưa chạy |

---

### 2.3. Module Masking & Alarm — Nguyễn Trung Kiên (`MK-01` đến `AL-07`)

| Mã Test | Tình huống kiểm thử | Thiết lập & Hành động | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| **MK-01** | Chặn tín hiệu bằng `SIG_BLOCK` | Đăng ký handler `SIGUSR1`, gọi `sigprocmask(SIG_BLOCK)` | `SIGUSR1` bị chặn, gửi signal không kích hoạt handler | Chưa chạy |
| **MK-02** | Bỏ chặn bằng `SIG_UNBLOCK` | Tiếp nối MK-01, gửi signal, sau đó unblock | Handler được gọi thực thi ngay sau khi unblock | Chưa chạy |
| **MK-03** | Thiết lập toàn bộ bằng `SIG_SETMASK`| Gọi `SETMASK` với tập bit mới | `blocked_signals` đổi chính xác theo mask mới | Chưa chạy |
| **MK-04** | Đọc mask cũ qua `oldset` | Gọi `sigprocmask` với `oldset` hợp lệ | Nhận đúng giá trị mask của trạng thái trước đó | Chưa chạy |
| **MK-05** | Ngăn chặn việc chặn `SIGKILL` | Cố tình truyền bit `SIGKILL` vào `set` | Bit `SIGKILL` bị `sig_sanitize` loại bỏ, không thể chặn | Chưa chạy |
| **MK-06** | Tham số `how` không hợp lệ | Truyền `how = 99` | Syscall trả về `-1`, mask không bị biến đổi | Chưa chạy |
| **MK-07** | Con trỏ `set`/`oldset` rác | Truyền địa chỉ bộ nhớ không hợp lệ | Syscall phát hiện lỗi copy và trả về `-1` | Chưa chạy |
| **AL-01** | Hẹn giờ cơ bản `alarm(n)` | Đặt handler `SIGALRM`, gọi `alarm(5)` | Handler được gọi sau khoảng 5 ticks | Chưa chạy |
| **AL-02** | Hủy hẹn giờ qua `alarm(0)` | Gọi `alarm(50)`, sau đó gọi `alarm(0)` | Hủy thành công, không có `SIGALRM` nào kích hoạt | Chưa chạy |
| **AL-03** | Ghi đè hẹn giờ cũ | Gọi `alarm(50)`, sau đó gọi `alarm(10)` | Trả về số tick còn lại của lần trước (~50), deadline mới = 10 | Chưa chạy |
| **AL-04** | Demo Periodic Timer (re-arm) | Trong handler `SIGALRM`, gọi lại `alarm(5)` | Handler được gọi đều đặn theo chu kỳ ~5 ticks | Chưa chạy |
| **AL-05** | Kết hợp `alarm` và `sigprocmask` | Block `SIGALRM`, gọi `alarm(3)`, chờ 5 ticks | Alarm hết hạn, `SIGALRM` giữ pending; unblock thì handler chạy | Chưa chạy |
| **AL-06** | Độc lập đa CPU với Alarm | Chạy test alarm trên `CPUS=1` và `CPUS=4` | Tốc độ trôi deadline không bị phụ thuộc số lượng CPU | Chưa chạy |
| **AL-07** | Fork không thừa kế Alarm | Cha gọi `alarm(50)`, gọi `fork()` | Tiến trình con có `alarm_deadline == 0`, không bị kích hoạt nhầm | Chưa chạy |

---

### 2.4. Nhóm Tích hợp Chéo & Hồi quy Baseline (Integration & Regression)

| Mã Test | Tình huống kiểm thử | Thiết lập & Hành động | Kết quả mong đợi | Trạng thái |
|---|---|---|---|---|
| **IN-01** | Chu trình đầy đủ | `sigaction` + `sigprocmask` + `alarm` + `sigreturn` | Tất cả hoạt động trơn tru trong một tiến trình | Chưa chạy |
| **IN-02** | Alarm kích hoạt khi lồng | Đang chạy Handler `SIGUSR1`, timer `SIGALRM` hết hạn | Lồng tầng handler thứ 2 an toàn, unwind đúng thứ tự | Chưa chạy |
| **IN-03** | Fork thừa kế Action & Mask | Cha cài đặt handler và mask, sau đó `fork()` | Con nhận tín hiệu và thực thi đúng handler của cha | Chưa chạy |
| **IN-04** | Exec reset Handler về DFL | Đăng ký handler, gọi `exec("ls", ...)` | Binary mới nhận signal sẽ xử lý theo `SIG_DFL` | Chưa chạy |
| **RG-01** | Toàn bộ Usertests xv6 | Chạy lệnh `usertests` trên terminal QEMU | Toàn bộ các bài test gốc của xv6 đạt `ALL TESTS PASSED` | Chưa chạy |
| **RG-02** | Stress Shell & Pipe | Chạy vòng lặp tạo tiến trình con và pipe liên tục | Hệ thống không cạn kiệt tài nguyên proc/stack | Chưa chạy |

---

## 3. Kịch bản Thực hiện 3 Yêu cầu Demo Cốt lõi

### Demo 1: Bắt phím `Ctrl+C` ngắt tiến trình lặp vô tận
- **Chương trình user:** `user/loop_demo.c`
- **Kịch bản:** Tiến trình in ra màn hình `Running...` liên tục mỗi giây. Người dùng nhấn tổ hợp phím `Ctrl+C` trên bàn phím:
  - Bàn phím gửi ngắt $\to$ Kernel định tuyến `SIGINT` tới tiến trình.
  - Handler in thông báo: `[DEMO 1] Caught Ctrl+C (SIGINT)! Cleaning up and exiting...`.
  - Tiến trình thoát sạch sẽ.

### Demo 2: Timer `alarm()` chạy tuần hoàn (Periodic Timer)
- **Chương trình user:** `user/alarm_demo.c`
- **Kịch bản:** Tiến trình gọi `alarm(10)` (khoảng 1 giây).
  - Khi hết hạn, handler `SIGALRM` được kích hoạt, in thông báo `[DEMO 2] Tick: 1s elapsed`.
  - Bên trong handler, gọi lại `alarm(10)` (Re-arm).
  - Chương trình lặp lại đều đặn 5 lần rồi tự động hủy bằng `alarm(0)`.

### Demo 3: Tín hiệu bị Chặn được Delivery ngay sau khi Unblock
- **Chương trình user:** `user/mask_demo.c`
- **Kịch bản:**
  - Tiến trình gọi `sigprocmask(SIG_BLOCK)` chặn `SIGUSR1`.
  - Tiến trình tự gửi tín hiệu cho chính mình: `kill(getpid(), SIGUSR1)`.
  - Kiểm tra handler: **Chưa chạy**; in thông báo `Signal is masked, safely continuing work...`.
  - Tiến trình gọi `sigprocmask(SIG_UNBLOCK)`.
  - Ngay sau lời gọi unblock: Handler kích hoạt thành công và in `[DEMO 3] Handler executed immediately after unmask!`.

