# Hướng dẫn AI làm việc với dự án POSIX-style signals trên xv6-riscv

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
- Mỗi thành viên phải hiểu và giải thích được code mình nộp.
- Không tự push, merge, tạo PR, thay đổi GitHub settings hoặc gửi thông tin ra ngoài nếu người dùng chưa yêu cầu.

## 3. Bắt đầu mỗi task

Trước khi sửa code:

1. Đọc `AGENTS.md`, `README.md`, `Makefile`, tài liệu thiết kế/API và handoff có liên quan.
2. Kiểm tra `git status`, branch, commit hiện tại và diff. Không xóa hoặc ghi đè thay đổi của thành viên khác; không tùy tiện `reset --hard`, `clean -fd` hay force-push.
3. Xác minh **commit/tag xv6 baseline** mà nhóm và giảng viên chọn. Không tự nâng cấp sang HEAD mới của MIT.
4. Đọc code **trên đúng baseline**: `kernel/proc.c`, `kernel/proc.h`, `kernel/trap.c`, `kernel/syscall.c`, `kernel/sysproc.c`, `kernel/trampoline.S`, `user/` và các file liên quan. Đường dẫn thực tế có thể khác theo phiên bản.
5. Xác định rõ mục tiêu, input/output, syscall ABI, thay đổi trạng thái process, invariant, dependency, test tối thiểu và tiêu chí hoàn thành.
6. Kiểm tra lệnh build/test thực sự được Makefile hỗ trợ; không giả định test hoặc syscall mới đã tồn tại.

Nếu không thể chạy QEMU hoặc thiếu toolchain, báo rõ giới hạn. **Build thành công không đồng nghĩa test runtime thành công.** Không tự cài đặt công cụ hệ thống nếu chưa được phép.

## 4. Phân công và phối hợp

| Thành viên | Module chính | Trách nhiệm liên quan |
|---|---|---|
| **Phạm Lê Quyền** | Signal sending & delivery | `kill()`, pending signals, lựa chọn signal, tích hợp delivery trong kernel |
| **Trần Việt Hưng** | Signal handler & context | `sigaction()`, handler frame trên user stack, `sigreturn()`, khôi phục thanh ghi, nested handlers |
| **Nguyễn Trung Kiên** | Signal masking & timer | `sigprocmask()`, `alarm()`, timer tick, test masked/pending signals |

- Mỗi thành viên chịu trách nhiệm **code, test và tài liệu** của module mình; không đẩy toàn bộ việc test sang Kiên.
- Review chéo mặc định: **Hưng review Quyền; Kiên review Hưng; Quyền review Kiên**. Review do AI gợi ý không thay cho review thực tế của thành viên.
- **Quyền ↔ Kiên:** thống nhất biểu diễn `pending`, `blocked`, điều kiện chọn signal và thời điểm delivery.
- **Quyền ↔ Hưng:** thống nhất cách kernel chuyển từ delivery sang handler, sửa trapframe/PC/SP và cách quay lại user mode.
- **Hưng ↔ Kiên:** thống nhất mask khi vào handler, khôi phục mask khi `sigreturn()` và hành vi nested handler.
- Các file `kernel/proc.h`, `kernel/proc.c`, `kernel/trap.c`, `kernel/syscall.*` có thể được nhiều người sửa: **thống nhất API và phối hợp PR trước khi thay đổi**. Phân công module không có nghĩa sở hữu độc quyền file.
- Không tự tạo nhiều AI agents hoặc thay đổi phân công nếu chưa có yêu cầu.

## 5. Quy tắc sửa code và Git

- Ưu tiên patch nhỏ, mỗi PR có mục tiêu và test rõ ràng; không format/refactor toàn repository.
- Giữ phong cách C và cách xử lý lỗi hiện có của xv6; không dùng API libc của host trong kernel.
- Kiểm tra user pointers, địa chỉ stack, kích thước frame, alignment, overflow và mọi nhánh lỗi.
- Khi thay đổi ABI, syscall numbers, cấu trúc dùng chung hoặc trapframe, cập nhật tất cả caller, user wrappers, test và tài liệu liên quan.
- Không để TODO thay thế một tính năng core rồi báo đã hoàn thành.
- Không bỏ test, sửa expected result tùy tiện hoặc tắt kiểm tra để che lỗi.
- Branch gợi ý: `feature/signal-delivery`, `feature/signal-handler`, `feature/signal-mask-alarm`; dùng branch nhỏ hơn cho từng Issue khi phù hợp.
- Commit mô tả rõ thay đổi; tạo Pull Request để review trước khi merge vào `main`.
- Không commit token, mật khẩu, secret, file build hoặc log chứa thông tin nhạy cảm.

## 6. Invariant kernel và signal lifecycle

### 6.1. Process state và locking

- Xác định context của từng thao tác: syscall, user trap, kernel trap, timer interrupt, process exit hoặc wakeup. Không giả định luôn có process hiện tại.
- Pending và blocked signals phải có biểu diễn rõ ràng, được đồng bộ đúng khi nhiều CPU cùng truy cập; không dùng `volatile` thay cho locking/atomic.
- Định nghĩa rõ signal nào hợp lệ, signal nào có thể bị mask, hành vi signal lặp lại và cách xử lý process đã exit/zombie.
- Không gọi code có thể sleep, yield hoặc block khi đang giữ spinlock hay trong interrupt context không cho phép.
- Kiểm tra lock order, balanced acquire/release, race giữa `kill`, unmask, timer và delivery.
- Không đưa handler user vào chạy trực tiếp trong kernel context.

### 6.2. Trapframe, user stack và context restoration

- Chỉ chuyển điều khiển đến handler ở điểm trở về user mode phù hợp với trap path thực tế.
- Handler frame phải được ghi vào **user stack hợp lệ**, kiểm tra địa chỉ, alignment, quyền truy cập và thất bại `copyout` (nếu dùng).
- Trước khi vào handler, lưu đủ trạng thái cần thiết để `sigreturn()` khôi phục **chính xác các thanh ghi và PC/SP** theo ABI đã thống nhất.
- Không tin dữ liệu hoặc địa chỉ do user cung cấp mà không xác minh; không để frame bị giả mạo dẫn đến kernel memory corruption.
- Nested handlers cần chính sách rõ: thứ tự frame, giới hạn độ sâu nếu có, signal nào được phép lồng và cách xử lý stack không đủ chỗ.
- Trạng thái mask trước khi chạy handler và sau `sigreturn()` phải nhất quán; không vô tình làm mất pending signals.
- Phân biệt user trap và kernel trap; không sửa đường kernel trap khi chưa chứng minh cần thiết.

### 6.3. Lifetime và lỗi

- Định nghĩa cách khởi tạo/copy/reset signal state khi `allocproc`, `fork`, `exec`, `exit` và `freeproc` theo phạm vi đã chốt.
- Không giữ pointer tới user stack hoặc process sau khi lifetime kết thúc.
- Có cách xử lý an toàn khi handler không hợp lệ, stack lỗi, syscall sai tham số, signal không hợp lệ hoặc alarm hết hạn gần lúc process thoát.
- Nếu semantics chưa được đề bài quy định (ví dụ default action, ignored signals, restart syscall), ghi thành câu hỏi thiết kế; không âm thầm coi hành vi Linux/POSIX đầy đủ là yêu cầu.

## 7. Yêu cầu riêng của `kill()`, pending và delivery — Quyền

- Xác minh `kill()` sẵn có trên baseline xv6: có thể đã mang nghĩa **đánh dấu process bị killed**, không phải POSIX signal API. Không ghi đè semantics cũ khi chưa có phương án tương thích.
- Chốt chữ ký syscall, ý nghĩa tham số, signal numbers và error behavior với cả nhóm.
- Pending set được cập nhật an toàn, không làm mất signal do race giữa sender, delivery và unmask.
- Chọn signal từ `pending & ~blocked` theo chính sách đã công bố; xử lý nhiều pending signals một cách xác định.
- Delivery phải phối hợp với action/handler do Hưng quản lý và mask do Kiên quản lý.
- Không delivery vào user stack khi process không ở trạng thái phù hợp; không thực thi handler từ kernel.
- Test tối thiểu: gửi signal hợp lệ/không hợp lệ, PID không tồn tại, nhiều pending signals, masked signal, delivery sau unmask và tương tác với process exit.

## 8. Yêu cầu riêng của `sigaction()`, handler frame, `sigreturn()` — Hưng

- Chốt cấu trúc action, cách biểu diễn handler/default/ignore (nếu thuộc phạm vi), các flags được hỗ trợ và giá trị lỗi.
- Xác minh ABI RISC-V, trapframe layout, cách truyền tham số cho handler, stack alignment và địa chỉ trở về/trampoline.
- Tạo handler frame chứa đủ context cần thiết; xác định ownership, validation và layout trước khi code.
- `sigreturn()` phải khôi phục đúng PC, SP, general-purpose registers và signal mask theo contract; tránh vô tình ghi đè giá trị thanh ghi bằng return value syscall thông thường.
- Nested handlers phải có chiến lược stack frame và thứ tự unwind rõ; không làm hỏng context của handler bên ngoài.
- Test tối thiểu: handler chạy đúng một lần, quay lại đúng vị trí, kiểm tra registers, nested handler, stack boundary và `sigreturn` không hợp lệ.

## 9. Yêu cầu riêng của `sigprocmask()`, `alarm()` — Kiên

- Chốt API block/unblock/setmask và giá trị mask hợp lệ; định nghĩa rõ các signal không được block nếu thiết kế có quy định.
- Signal bị block **vẫn pending** theo semantics đã thống nhất; unmask kích hoạt delivery tại điểm an toàn, không gọi handler ngay trong syscall/interrupt context một cách tùy tiện.
- `alarm()` phải có đơn vị thời gian/tick, chính sách thay thế alarm cũ, xử lý zero, overflow và lifecycle process rõ ràng.
- Timer interrupt chỉ cập nhật trạng thái tối thiểu cần thiết; không chạy handler user trực tiếp trong timer interrupt.
- Với demo periodic handler, nếu API `alarm()` chỉ hỗ trợ one-shot, chương trình demo cần tự re-arm theo thiết kế; không tự coi alarm là periodic mặc định.
- Test tối thiểu: block/unblock, pending khi masked, nhiều signals, alarm one-shot/re-arm, alarm bị thay thế/hủy, race với exit và kết hợp alarm + mask.

## 10. Build và kiểm thử

### 10.1. Quy trình cho mỗi patch

1. Ghi baseline commit, branch và cấu hình môi trường (compiler, QEMU, CPUS nếu liên quan).
2. Build bằng lệnh repo hỗ trợ (thường là `make qemu` hoặc lệnh tương ứng, nhưng phải kiểm tra Makefile).
3. Chạy test trực tiếp cho chức năng thay đổi, bao gồm error paths và boundary cases.
4. Với trapframe, locking hoặc timer, kiểm tra thêm các cấu hình CPU phù hợp, ưu tiên `CPUS=1` và `CPUS=4` khi môi trường hỗ trợ.
5. Chạy regression liên quan để phát hiện xv6 baseline bị hỏng; trước final cần có full `usertests` và stress phù hợp.
6. Lưu lệnh chạy, log thật, commit hash, kết quả PASS/FAIL và giới hạn kiểm thử.

**Không ghi PASS nếu chỉ compile; không ghi chức năng hoàn thành khi mới có pseudocode hoặc prototype.** Không giả định test runner, `make test` hoặc các user programs đã tồn tại.

### 10.2. Ma trận test mục tiêu

| Nhóm test | Tình huống cần bao phủ |
|---|---|
| Delivery | valid/invalid signal, PID không tồn tại, nhiều pending, gửi khi process đang chạy/ngủ |
| Handler | đăng ký handler, thay handler, handler return, bảo toàn registers/PC/SP |
| Nested | handler lồng nhau, thứ tự unwind, giới hạn user stack |
| Masking | block, unblock, pending trong lúc block, delivery sau unblock |
| Alarm | hết hạn, thay/hủy alarm, re-arm để demo periodic, alarm bị mask |
| Integration | `kill` + `sigaction` + `sigprocmask` + `alarm` + `sigreturn` |
| Regression | shell, process lifecycle, syscall cũ, `usertests` và stress thích hợp |

Mỗi test cần mô tả **setup → action → expected → actual → PASS/FAIL**. Không đặt số lượng test hoặc ngưỡng benchmark như yêu cầu của giảng viên nếu chưa được xác nhận.

### 10.3. CI và minh chứng

- Khi có runner ổn định, có thể bổ sung GitHub Actions để build và chạy test tự động; không tự nhận CI đã có.
- Giữ log khi test thất bại, phân biệt QEMU timeout, test timeout và kernel panic.
- Không dùng `retry until pass` rồi giấu lần thất bại.
- Không để GitHub Actions hoặc script tự dùng token/secret ngoài phạm vi được cấp.

## 11. Demo và đánh giá

Demo mục tiêu:

1. Chương trình user lặp; Ctrl+C dẫn đến signal và handler tương ứng theo đường input/console đã triển khai.
2. `alarm()` kích hoạt handler; nếu cần lặp định kỳ, handler hoặc chương trình user re-arm alarm theo semantics đã chốt.
3. Signal bị mask được giữ pending và được delivery sau khi unmask.

- Kiểm tra rõ xv6 baseline có hay chưa cơ chế biến Ctrl+C thành signal; nếu chưa, đây là phần tích hợp cần triển khai, không phải tính năng có sẵn.
- Mỗi demo có hướng dẫn build/run, expected output, actual log và commit tương ứng.
- Nếu đo latency/overhead, lưu môi trường và dữ liệu thô; không kết luận từ một lần chạy.
- Stretch SIGSEGV/fault address và interrupted syscall chỉ trình diễn khi thực sự triển khai và kiểm chứng.

## 12. Lịch và ưu tiên (kế hoạch nội bộ 8 tuần)

| Tuần | Quyền — Delivery | Hưng — Handler | Kiên — Mask/Alarm |
|---|---|---|---|
| 1 | Khảo sát `kill`, process, pending/delivery; thiết kế | Khảo sát `sigaction`, trapframe, user stack, `sigreturn`; thiết kế | Khảo sát `sigprocmask`, timer/alarm; lập test matrix |
| 2 | Chốt pending state và signal selection | Chốt action structure, handler context | Chốt mask và alarm state/tick |
| 3 | Triển khai `kill`, pending, kernel hooks | Triển khai `sigaction`, handler frame, `sigreturn` ban đầu | Triển khai `sigprocmask` |
| 4 | Hoàn thiện delivery có xét mask | Hoàn thiện context restoration | Triển khai `alarm`, timer tick |
| 5 | Nhiều pending và edge cases | Nested handlers và context stack | Test mask/pending/alarm |
| 6 | Tích hợp và sửa lỗi | Kiểm thử registers/context/nested | Integration/regression tests |
| 7 | Hỗ trợ demo Ctrl+C, stretch nếu còn thời gian | Sửa lỗi trap/context, hỗ trợ stretch | Hoàn thiện demo và regression |
| 8 | Đóng băng module, tài liệu | Đóng băng module, tài liệu | Test cuối, báo cáo và demo |

Đây là **kế hoạch đề xuất**, không phải bằng chứng tiến độ hay deadline giảng viên đã phê duyệt. Khi trễ tiến độ, ưu tiên core correctness, integration và test trước stretch.

## 13. Tài liệu, báo cáo tuần và bàn giao

Tận dụng cấu trúc repo hiện có. Khi cần, có thể dùng:

- `docs/signal-delivery.md`: thiết kế của Quyền.
- `docs/signal-handler.md`: thiết kế của Hưng.
- `docs/signal-mask-alarm.md`: thiết kế của Kiên.
- `docs/signal-design.md`: API/ABI chung, state layout, lifecycle, quyết định thiết kế.
- `docs/test-plan.md`: test matrix và cách chạy.
- `docs/weekly/week-01.md` và các tuần tiếp theo: báo cáo tiến độ có bằng chứng.

Không tạo tài liệu rỗng chỉ để đủ danh sách. Báo cáo tuần nên phân biệt: **đã nghiên cứu / đã thiết kế / đã code / đã build / đã test / đã merge**.

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

Đồ án chỉ được coi là hoàn thành khi các giao diện signal cốt lõi hoạt động phối hợp, handler frame và register restoration đúng, masked/pending/nested/alarm tests có bằng chứng, demo chạy được và báo cáo phản ánh chính xác code đã merge. File này không thay thế yêu cầu hoặc quyết định chấm điểm của giảng viên.
