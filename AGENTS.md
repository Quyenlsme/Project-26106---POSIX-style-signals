# Hướng dẫn AI làm việc với dự án POSIX-style signals trên xv6-riscv

*Cập nhật: 26/11/2026, đầu tuần 6 (`main@4803454`). Bản đầu: 24/10/2026.*

## 1. Context và mục tiêu

Dự án môn Hệ điều hành: **Đề tài 26106 — POSIX-style signals (Processes, ★★)** trên xv6-riscv.

Repository nhóm: https://github.com/Quyenlsme/Project-26106---POSIX-style-signals

Thành viên: **Phạm Lê Quyền, Trần Việt Hưng, Nguyễn Trung Kiên**.

### Yêu cầu cốt lõi

1. Hỗ trợ các giao diện signal theo phạm vi đề tài: `sigaction()`, `kill()`, `sigreturn()`, `sigprocmask()`, `alarm()`; triển khai kernel-side và user wrappers cần thiết.
2. Thiết lập **signal handler frame trên user stack**, chuyển điều khiển đến handler và **khôi phục chính xác trạng thái thanh ghi** khi `sigreturn()`.
3. Quản lý pending signals, blocked signals và lựa chọn signal đủ điều kiện delivery.
4. Kiểm thử nested handlers, masked/pending signals, alarm và các tình huống biên.
5. Demo: Ctrl+C kích hoạt handler trong chương trình lặp; `alarm` kích hoạt handler định kỳ theo thiết kế; signal bị mask được delivery sau khi unmask.

**Stretch (chỉ làm sau khi core ổn định):** hỗ trợ SIGSEGV kèm fault address; syscall bị ngắt trả lỗi theo semantics được nhóm thống nhất. Không tự nhận stretch là bắt buộc nếu chưa có xác nhận.

Mục tiêu: thay đổi kernel nhỏ gọn, đúng semantics đã thống nhất, an toàn trên xv6-riscv, giải thích được và có test tái lập. **Không giả định xv6 đã hỗ trợ đầy đủ POSIX signals**; phạm vi chính xác, signal numbers, error codes và ABI phải được chốt theo yêu cầu môn học.

File này mô tả **quy tắc làm việc**, không chứng minh tính năng đã có hoặc test đã pass. Mọi trạng thái phải kiểm tra trực tiếp trên repository và môi trường chạy.

## 2. Ngôn ngữ và cách làm việc

- Trao đổi, giải thích, tài liệu nhóm bằng **tiếng Việt Unicode**; tên biến/hàm/API dùng tiếng Anh, comment theo phong cách repository.
- Khi được giao triển khai, hoàn thành thay đổi nhỏ có thể review và báo kết quả thực tế; không chỉ đưa kế hoạch.
- Không tự thay đổi yêu cầu, phân công, syscall ABI hoặc semantics khi chưa có sự thống nhất.
- Không thêm thư viện, framework hoặc subsystem ngoài phạm vi nếu xv6 hiện có đáp ứng.
- Không chép lời giải công khai làm phần tự triển khai. Ghi nguồn tham khảo và tuân thủ quy định học phần.
- Mỗi thành viên phải hiểu và giải thích được code mình nộp. Commit đứng tên người thực hiện; không re-author code của thành viên khác.
- Không tự push, merge, tạo PR, thay đổi GitHub settings hoặc gửi thông tin ra ngoài nếu người dùng chưa yêu cầu.

## 3. Bắt đầu mỗi task

Trước khi sửa code:

1. Đọc `AGENTS.md`, `README.md`, `Makefile`, `docs/signal-design.md` (quyết định D1–D6) và tài liệu module liên quan trong `docs/`.
2. Kiểm tra `git status`, branch, commit hiện tại và diff. Không xóa hoặc ghi đè thay đổi của thành viên khác; không tùy tiện `reset --hard`, `clean -fd` hay force-push.
3. **Baseline xv6:** MIT xv6-riscv tại commit `f780d78` (15/10/2026), nhập vào repo ở `0612d85` (PR #1). Không tự nâng cấp sang HEAD mới của MIT.
4. Đọc code **trên đúng baseline**: `kernel/proc.c`, `kernel/proc.h`, `kernel/trap.c`, `kernel/syscall.c`, `kernel/sysproc.c`, `kernel/trampoline.S`, `kernel/console.c`, `user/` và các file liên quan.
5. Xác định rõ mục tiêu, input/output, syscall ABI, thay đổi trạng thái process, invariant, dependency, test tối thiểu và tiêu chí hoàn thành.
6. Kiểm tra lệnh build/test thực sự được Makefile hỗ trợ (mục 10).

Nếu không thể chạy QEMU hoặc thiếu toolchain, báo rõ giới hạn. **Build thành công không đồng nghĩa test runtime thành công.** Không tự cài đặt công cụ hệ thống nếu chưa được phép.

## 4. Phân công và phối hợp

| Thành viên | Module chính | Trách nhiệm liên quan |
|---|---|---|
| **Phạm Lê Quyền** | Signal sending & delivery | `kkill()`, `sig_send()`, `sig_deliver()`, pending signals, lựa chọn signal, hành động mặc định, vòng đời process và wakeup (`sleep_prepare_intr`), lock order trong `signal-core-locking.md`; test `killtest` |
| **Trần Việt Hưng** | Signal handler & context | `sys_sigaction()`, `sig_setup_frame()`, `sys_sigreturn()`, `struct sigframe`, khôi phục thanh ghi, nested handlers, `sa_mask`; test `regtest` |
| **Nguyễn Trung Kiên** | Mask, timer, shell và tích hợp | `sys_sigprocmask()`, `sys_alarm()`, `sig_alarm_check()`, `pause`; Ctrl+C → SIGINT (`fg_pid`, `console_setfg`, syscall `setfg`, `sh.c`); test `sigtest`, `pausetest`, 3 chương trình demo; tự động hóa test và CI |

- Mỗi thành viên chịu trách nhiệm **code, test và tài liệu** của module mình; không đẩy toàn bộ việc test sang Kiên.
- Review chéo mặc định: **Hưng review Quyền; Kiên review Hưng; Quyền review Kiên**. Review do AI gợi ý không thay cho review thực tế của thành viên.
- **Quyền ↔ Kiên:** thống nhất biểu diễn `pending_signals`, `blocked_signals`, điều kiện chọn signal (`pending & ~blocked & SUPPORTED_MASK`) và thời điểm delivery.
- **Quyền ↔ Hưng:** thống nhất cách kernel chuyển từ `sig_deliver()` sang `sig_setup_frame()`, sửa trapframe/PC/SP và cách quay lại user mode.
- **Hưng ↔ Kiên:** thống nhất mask khi vào handler (`blocked |= sa_mask | (1 << sig)`), khôi phục mask khi `sigreturn()` và hành vi nested handler.
- Các file `kernel/proc.h`, `kernel/proc.c`, `kernel/trap.c`, `kernel/syscall.*`, `kernel/signal.h` có thể được nhiều người sửa: **thống nhất API và phối hợp PR trước khi thay đổi**. Phân công module không có nghĩa sở hữu độc quyền file.
- File test: Quyền chỉ sửa `user/killtest.c`; Hưng chỉ sửa `user/regtest.c`; Kiên sửa `user/sigtest.c`, `user/pausetest.c`, các demo, `test-xv6.py` và `.github/workflows/test.yml`.
- Không tự tạo nhiều AI agents hoặc thay đổi phân công nếu chưa có yêu cầu.

## 5. Quy tắc sửa code và Git

- Ưu tiên patch nhỏ, mỗi PR có mục tiêu và test rõ ràng; không format/refactor toàn repository.
- Giữ phong cách C và cách xử lý lỗi hiện có của xv6; không dùng API libc của host trong kernel.
- **Định dạng code:** chạy `make fmt` (clang-format theo `.clang-format` của repo) trước mỗi commit. CI chạy `make fmt && git diff --exit-code`, code sai định dạng làm CI đỏ. Chạy clang-format từ thư mục gốc repo để nó đọc đúng `.clang-format`.
- Kiểm tra user pointers, địa chỉ stack, kích thước frame, alignment, overflow và mọi nhánh lỗi.
- Khi thay đổi ABI, syscall numbers, cấu trúc dùng chung hoặc trapframe, cập nhật tất cả caller, user wrappers, test và tài liệu liên quan.
- Không để TODO thay thế một tính năng core rồi báo đã hoàn thành.
- Không bỏ test, sửa expected result tùy tiện hoặc tắt kiểm tra để che lỗi.
- **Branch:** mỗi người một nhánh theo tuần, tên `feature/<module>-w<N>` (ví dụ `feature/signal-core-w5`, `feature/sa-mask-w5`, `feature/autotest-w5`); xuất phát từ `main` đầu tuần.
- **Pull Request:** mọi thay đổi vào `main` đi qua PR có review chéo; không commit hay merge thẳng vào `main`. Thứ tự merge cuối tuần: **Quyền → Hưng → Kiên** (Kiên merge cuối để CI chạy trên toàn bộ test mới). Không push thêm commit lên nhánh sau khi PR đã merge; mở PR mới.
- Trước khi mở PR: `make clean && make qemu` trên `CPUS=1` và `CPUS=4`, chạy test của module mình và `usertests`.
- Commit mô tả rõ thay đổi theo dạng `loại(phạm vi): nội dung` (ví dụ `test(signal-core): DL-14 gửi signal tới tiến trình ZOMBIE`).
- Không commit token, mật khẩu, secret, file build hoặc log chứa thông tin nhạy cảm.

## 6. Invariant kernel và signal lifecycle

Các quyết định đã chốt nằm trong `docs/signal-design.md` (D1–D6) và là nguồn chân lý duy nhất. Tóm tắt những điểm mọi thay đổi phải giữ:

- Bitmask 32-bit cho `pending_signals` và `blocked_signals`; signal hợp lệ là `SUPPORTED_MASK`; `SIGKILL` không bắt, không chặn, không bỏ qua được (`UNBLOCKABLE_MASK`).
- Signal được giao duy nhất tại điểm trở về user mode (`sig_deliver()`); không chạy handler user trong kernel hay interrupt context.
- Nhiều signal cùng pending được giao theo số hiệu tăng dần (quyết định của nhóm, không phải yêu cầu POSIX).
- Thứ tự khóa toàn hệ thống: `tickslock → p->lock` và `cons.lock → p->lock`; không bao giờ lấy theo chiều ngược. `kkill()` chỉ dùng spinlock, không `sleep()`, nên gọi được từ ngắt.
- Trường cần `p->lock`: `pending_signals`, `alarm_deadline`. Trường riêng của tiến trình: `blocked_signals`, `sig_actions`.
- `fork`: con kế thừa mask và handler, không kế thừa pending và alarm. `exec`: handler về `SIG_DFL`, giữ `SIG_IGN`, hủy alarm.
- User stack là 1 trang (`USERSTACK = 1`); khi không đủ chỗ đặt `sigframe`, tiến trình bị kết thúc với `-1`, kernel không panic.

### 6.1. Process state và locking

- Xác định context của từng thao tác: syscall, user trap, kernel trap, timer interrupt, process exit hoặc wakeup. Không giả định luôn có process hiện tại.
- Không dùng `volatile` thay cho locking/atomic.
- Không gọi code có thể sleep, yield hoặc block khi đang giữ spinlock hay trong interrupt context không cho phép.
- Kiểm tra lock order, balanced acquire/release, race giữa `kill`, unmask, timer và delivery. Hai khe lost wakeup đã đóng (`p->chan = 0` trong `sig_send`, `sleep_prepare_intr`); thay đổi ở `sleep`/`wakeup`/`sig_send` phải chạy lại `killtest` T10 (500 vòng) trên `CPUS=4`.

### 6.2. Trapframe, user stack và context restoration

- Handler frame phải được ghi vào **user stack hợp lệ**, kiểm tra địa chỉ, alignment 16 byte, quyền truy cập và thất bại `copyout`.
- `sigreturn()` khôi phục 32 thanh ghi và mask từ `sigframe`; dispatcher syscall không ghi đè `a0` cho `SYS_sigreturn`.
- Không tin dữ liệu hoặc địa chỉ do user cung cấp mà không xác minh; `sigreturn` với frame không hợp lệ kết thúc tiến trình, không làm hỏng kernel.
- Nested handlers: thứ tự unwind LIFO, mask trở lại giá trị ban đầu sau mỗi tầng.
- Phân biệt user trap và kernel trap; không sửa đường kernel trap khi chưa chứng minh cần thiết.

### 6.3. Lifetime và lỗi

- Khởi tạo/copy/reset signal state khi `allocproc`, `fork`, `exec`, `exit` và `freeproc` theo D6.
- `kill` tới tiến trình `UNUSED`/`ZOMBIE` trả `-1`; `wait()` vẫn nhận đúng exit status.
- Nếu semantics chưa được đề bài quy định (ví dụ restart syscall, EINTR), ghi thành câu hỏi thiết kế; không âm thầm coi hành vi Linux/POSIX đầy đủ là yêu cầu.

## 7. Yêu cầu riêng của `kill()`, pending và delivery — Quyền

- Chữ ký `int kill(int pid, int sig)` thay `kill(pid)` gốc; `SIGKILL` giữ đường `p->killed = 1` + đánh thức của xv6.
- Pending set được cập nhật an toàn, không làm mất signal do race giữa sender, delivery và unmask.
- Chọn signal từ `pending & ~blocked` theo chính sách đã công bố; xử lý nhiều pending signals một cách xác định.
- Delivery phối hợp với action/handler do Hưng quản lý và mask do Kiên quản lý.
- Syscall đang chờ (`read`, `pause`, `wait`) bị signal thường ngắt trả `-1` (DL-10); semantics EINTR đầy đủ là stretch.
- Test tối thiểu: signal hợp lệ/không hợp lệ/không hỗ trợ, PID không tồn tại, nhiều pending, masked signal, delivery sau unmask, ZOMBIE, SIGKILL khi chặn hết, tự gửi, hành vi mặc định của từng signal.

## 8. Yêu cầu riêng của `sigaction()`, handler frame, `sigreturn()` — Hưng

- `struct sigaction` gồm `sa_handler`, `sa_mask`, `sa_flags` (bắt buộc 0), `sa_restorer`; `struct sigframe` gồm `saved_tf`, `saved_mask`.
- Trước khi vào handler: `a0 = signum`, `ra = sa_restorer`, `epc = sa_handler`, `sp = (sp − sizeof(sigframe)) & ~0xF`.
- `sigreturn()` khôi phục đúng PC, SP, thanh ghi và mask; tránh ghi đè `a0` bằng return value syscall.
- Nested handlers có chiến lược stack frame và thứ tự unwind rõ; không làm hỏng context của handler bên ngoài.
- Test tối thiểu: handler chạy đúng một lần, quay lại đúng vị trí, bảo toàn thanh ghi caller-saved và callee-saved, nested nhiều tầng, `sa_mask`, tràn user stack, `sigreturn` không hợp lệ, handler kích hoạt từ alarm lặp.

## 9. Yêu cầu riêng của `sigprocmask()`, `alarm()`, Ctrl+C — Kiên

- `sigprocmask(how, set, oldset)` với `SIG_BLOCK`/`SIG_UNBLOCK`/`SIG_SETMASK`; `SIGKILL` bị lọc khỏi mask.
- Signal bị block **vẫn pending**; unmask kích hoạt delivery tại điểm trở về user mode.
- `alarm(n)` tính deadline tuyệt đối `ticks + n`, so sánh có dấu trong `clockintr` trên CPU 0; `alarm(0)` hủy; alarm mới thay alarm cũ; không kế thừa qua `fork`, hủy khi `exec`.
- Timer interrupt chỉ đặt bit pending; không chạy handler user trong timer interrupt. Demo periodic tự re-arm trong handler.
- **Ctrl+C:** `consoleintr()` nhận `0x03`, in `^C`, gọi `kkill(fg_pid, SIGINT)` dưới `cons.lock` (hoặc in lại `$ ` nếu `fg_pid == 0`); `sh.c` gọi `setfg()` ở cả con (`getpid()`) và cha (`pid`) sau `fork`, rồi `setfg(0)` sau `wait`. Giới hạn đã ghi: chỉ ngắt con trực tiếp của shell (chưa có process group cho pipeline).
- Test tối thiểu: block/unblock, pending khi masked, nhiều signals, alarm one-shot/re-arm/thay/hủy, race với exit, alarm + mask, 4 tình huống Ctrl+C (tại dấu nhắc, có handler, `SIG_DFL`, tiến trình chạy nền `&`).

## 10. Build và kiểm thử

### 10.1. Lệnh và chương trình test hiện có

| Lệnh / chương trình | Mục đích |
|---|---|
| `make clean && make qemu CPUS=1` / `CPUS=4` | Build và boot; mọi test chạy ở cả hai cấu hình (mặc định `CPUS=3`) |
| `make fmt` | clang-format toàn bộ `kernel/`, `user/`, `mkfs/` |
| `killtest` | Signal core, mã DL-xx (Quyền) |
| `regtest` | Handler, mã HD-xx và REG-xx (Hưng) |
| `sigtest`, `pausetest` | Mask, alarm, pause, mã MK/AL/PA-xx (Kiên) |
| `ctrlc_demo [dfl]`, `alarm_demo`, `mask_demo` | 3 demo bắt buộc; kịch bản trong `docs/test-plan.md` §3 |
| `usertests -q`, `forktest` | Hồi quy xv6 gốc (RG-01) |
| `./test-xv6.py signals`, `./test-xv6.py usertests`, `./test-xv6.py crash` | Bước CI; cần Python 3 và QEMU trên host |

Thoát QEMU: `Ctrl+A` rồi `X`. Ctrl+C trong QEMU được gửi vào xv6 (demo), không thoát QEMU.

### 10.2. Quy trình cho mỗi patch

1. Ghi baseline commit, branch và cấu hình môi trường (compiler, QEMU, CPUS).
2. `make fmt`, rồi `make clean && make qemu`.
3. Chạy test của chức năng thay đổi, gồm error paths và boundary cases.
4. Với trapframe, locking hoặc timer, chạy trên cả `CPUS=1` và `CPUS=4`.
5. Chạy `usertests -q` để phát hiện xv6 baseline bị hỏng; trước final cần full `usertests` và stress.
6. Lưu lệnh chạy, log thật, commit hash, kết quả PASS/FAIL và giới hạn kiểm thử.

**Không ghi PASS nếu chỉ compile; không ghi chức năng hoàn thành khi mới có pseudocode hoặc prototype.** Một ca test chỉ PASS khi đã chạy thật trên QEMU, log khớp kỳ vọng, không panic, và ghi theo dạng **Mã test → Setup → Action → Expected → Actual → PASS/FAIL** (`docs/test-plan.md` §1.2).

### 10.3. CI và minh chứng

- CI hiện có (`.github/workflows/test.yml`, job linux): kiểm tra định dạng (`make fmt && git diff --exit-code`), build kernel và `fs.img`, `./test-xv6.py usertests`, `./test-xv6.py crash`, và `./test-xv6.py signals` (AU-01..03). Job macOS chỉ build.
- CI lỗi do `apt-get` hoặc DNS (`Temporary failure resolving`) không phải lỗi code: cancel và re-run, không sửa code để "fix".
- Giữ log khi test thất bại để chẩn đoán (không commit `test-xv6.out`), phân biệt QEMU timeout, test timeout và kernel panic. Không dùng `retry until pass` rồi giấu lần thất bại.
- Không để GitHub Actions hoặc script tự dùng token/secret ngoài phạm vi được cấp.

## 11. Demo và đánh giá

Ba demo bắt buộc, kịch bản và expected output trong `docs/test-plan.md` §3:

1. `ctrlc_demo`: chương trình lặp, Ctrl+C gọi handler SIGINT rồi thoát sạch; `ctrlc_demo dfl` bị kết thúc; Ctrl+C tại dấu nhắc không giết shell.
2. `alarm_demo`: handler SIGALRM tự re-arm `alarm()` để chạy định kỳ.
3. `mask_demo`: signal bị mask giữ pending và được delivery ngay sau khi unmask.

- Mỗi demo có hướng dẫn build/run, expected output, actual log và commit tương ứng.
- Nếu đo latency/overhead, lưu môi trường và dữ liệu thô; không kết luận từ một lần chạy.
- Stretch SIGSEGV/fault address và interrupted syscall chỉ trình diễn khi thực sự triển khai và kiểm chứng.

## 12. Lịch và ưu tiên

Khung thời gian và phân công theo **kế hoạch 8 tuần** của nhóm (`Project_26106_Ke_hoach_8_tuan`). Khi trễ tiến độ, ưu tiên core correctness, integration và test trước stretch. Tuần 8 không thêm tính năng lớn vào `main`.

## 13. Tài liệu và bàn giao

Thư mục `docs/` chỉ giữ các tài liệu thiết kế, phân tích kỹ thuật và kế hoạch kiểm thử cốt lõi:

- `docs/signal-design.md`: API/ABI chung, cấu trúc dữ liệu, lifecycle, quyết định D1–D6 và 10 quy tắc hệ thống. **Nguồn chân lý duy nhất.**
- `docs/signal-delivery.md`, `docs/signal-core-locking.md`: thiết kế và phân tích khóa của Quyền.
- `docs/signal-handler.md`: thiết kế của Hưng.
- `docs/signal-mask-alarm.md`: thiết kế của Kiên, gồm Ctrl+C (§9).
- `docs/test-plan.md`: ma trận kiểm thử theo mã, kịch bản 3 demo, trạng thái PASS/FAIL.

Không tạo tài liệu rỗng chỉ để đủ danh sách. Khi báo cáo tiến độ, phân biệt rõ: **đã nghiên cứu / đã thiết kế / đã code / đã build / đã test / đã merge**.

Kết thúc mỗi task, báo cáo:

1. Đã làm gì và mục tiêu giải quyết.
2. File, branch, commit hoặc PR liên quan.
3. API/invariant đã thay đổi và tác động đến hai module còn lại.
4. Lệnh build/test đã chạy, cấu hình và kết quả thật.
5. Chưa kiểm chứng, lỗi còn lại, bước tiếp theo.

Mẫu handoff:

```text
Thành viên / task / branch / commit:
Context và mục tiêu:
Thay đổi đã thực hiện:
API / invariant / file liên quan:
Lệnh build/test, cấu hình, kết quả thực tế:
Chưa kiểm chứng / blocker:
Ảnh hưởng đến Quyền / Hưng / Kiên:
Bước tiếp theo và tiêu chí hoàn thành:
```

## 14. Điều kiện hoàn thành

Một task chỉ hoàn thành khi đáp ứng phạm vi được giao, thay đổi có thể review, có kiểm thử phù hợp rủi ro, không phá hành vi xv6 cũ, tài liệu nhất quán và người thực hiện hiểu code.

Đồ án chỉ được coi là hoàn thành khi các giao diện signal cốt lõi hoạt động phối hợp, handler frame và register restoration đúng, masked/pending/nested/alarm tests có bằng chứng, 3 demo chạy được và báo cáo phản ánh chính xác code đã merge. File này không thay thế yêu cầu hoặc quyết định chấm điểm của giảng viên.
