# Design Thinking – Đề tài 26106: POSIX-style Signals trên xv6-riscv

* **Học phần:** Hệ điều hành – 20261
* **Nhóm:** Phạm Lê Quyền (signal core), Trần Việt Hưng (signal handler), Nguyễn Trung Kiên (mask, alarm, Ctrl-C)
* **Repository:** [Project-26106---POSIX-style-signals](https://github.com/Quyenlsme/Project-26106---POSIX-style-signals)
* **Cập nhật:** 22/11/2026, cuối tuần 5 (`main` đã gộp 3 nhánh tuần 5)
* **Tài liệu liên quan:** `Project_26106_Ke_hoach_8_tuan` (kế hoạch 8 tuần), `docs/signal-design.md` (quyết định thiết kế D1–D6), `docs/test-plan.md` (ma trận kiểm thử), `docs/weekly/` (báo cáo tuần)

Nhóm áp dụng design thinking theo năm giai đoạn Empathize → Define → Ideate → Prototype → Test. Kế hoạch 8 tuần cho khung thời gian và phân công; tài liệu thiết kế trong `docs/` ghi lại các quyết định; mỗi tuần là một vòng Prototype → Test, và vòng Test của tuần này sinh ra việc của tuần sau.

Sau 5 tuần (24/10–22/11/2026), nhóm đã đi trước kế hoạch 8 tuần: toàn bộ chức năng cốt lõi (`kill`, `sigaction`, `sigreturn`, `sigprocmask`, `alarm`, Ctrl-C → SIGINT) đã có trong `main` và được kiểm thử trên 1 và 4 CPU. Tuần 5 hoàn thiện ngữ nghĩa (signal không hỗ trợ, hành vi mặc định, `sa_mask`) và đưa toàn bộ test signal vào CI; các tuần còn lại dành cho timer, stress và ổn định.

---

## 1. Empathize – Ai cần signal và họ đang gặp khó gì

Kế hoạch 8 tuần (mục 1) nêu ba nhóm yêu cầu: chức năng cốt lõi, kiểm thử bắt buộc, và phần stretch chỉ làm sau khi core ổn định. Phía sau ba nhóm yêu cầu đó là bốn kiểu người dùng với nhu cầu khác nhau.

| Người dùng | Họ muốn làm gì | Trên xv6 gốc | Nguồn |
|---|---|---|---|
| Lập trình viên ứng dụng trên xv6 | Chương trình dọn dẹp rồi thoát khi bị ngắt; chạy việc định kỳ theo timer; tạm hoãn ngắt trong đoạn code quan trọng | `kill(pid)` luôn giết ngay, chỉ đặt `p->killed` (xv6 book §7.8); không có handler, `alarm`, mask | Kế hoạch 8 tuần, mục 1 "Chức năng cốt lõi" |
| Người dùng shell | Bấm Ctrl-C để dừng chương trình đang chạy mà shell vẫn sống | Ctrl-C chỉ là ký tự `0x03` đưa vào shell; shell chạy nó như một lệnh và in `exec failed` | Quan sát thật trên `main` trước khi merge `feature/ctrlc-sigint` (15/11) |
| Giảng viên, người chấm | Thấy đủ ba luồng demo và bằng chứng kiểm thử chạy lại được | Một thay đổi kernel chỉ mô tả bằng lời thì không kiểm chứng được | Kế hoạch 8 tuần, mục 1 "Kiểm thử bắt buộc" và mục 6 "Checklist trước khi nộp" |
| Nhóm phát triển (3 người, 3 module) | Sửa chung `struct proc`, trap path và `signal.h` mà không phá code của nhau | Race và deadlock chỉ lộ ra khi chạy nhiều CPU; merge tay dễ mất code | Kế hoạch 8 tuần, mục 3 và mục 4 "Giao diện module" |

**Quan sát từ code thật, không từ tài liệu:**

* Lost wakeup: signal gửi đúng lúc tiến trình vừa vào `read()` có thể bị mất. Chỉ lộ ra khi lặp 500 vòng trên `CPUS=4` (`killtest` T10, commit `8536e3a`, tuần 3).
* Hai khe mất signal khác nhau cần hai cách sửa: khe giữa `sleep_prepare` và `sleep` (xóa `p->chan`, commit `7d73a0a`) và khe giữa kiểm tra signal và `sleep_prepare` (`sleep_prepare_intr`, commit `8536e3a`).
* Commit 28/10–01/11 cho thấy mỗi người ban đầu viết đặc tả riêng; Kiên phải "đồng bộ đặc tả signal-handler khớp với cấu trúc của Kiên, Quyền" (`f4256ca`) trước khi code chung được. Nhu cầu "một nguồn chân lý" của nhóm là có thật.

---

## 2. Define – Vấn đề cần giải

**Phát biểu vấn đề:** lập trình viên và người dùng shell trên xv6 cần một cách báo sự kiện bất đồng bộ cho tiến trình, để tiến trình tự quyết định xử lý, hoãn hay bị kết thúc, mà kernel vẫn an toàn trên nhiều CPU và mỗi hành vi đều có test chứng minh.

**Câu hỏi "How might we", mỗi câu gắn với một module và một nhóm test trong ma trận kiểm thử tối thiểu (kế hoạch 8 tuần, mục 5):**

| # | How might we... | Module | Nhóm test |
|---|---|---|---|
| 1 | ...gửi signal tới đúng tiến trình, giữ pending, và xác định hành động mặc định? | Quyền | Signal core (DL-xx) |
| 2 | ...cho tiến trình chạy hàm của chính nó rồi quay lại đúng chỗ với mọi thanh ghi còn nguyên? | Hưng | Handler/return (HD-xx) |
| 3 | ...tạm chặn signal mà không làm mất nó, kể cả khi handler lồng nhau? | Hưng + Kiên | Mask/nested (MK-xx, HD-xx) |
| 4 | ...tạo signal từ timer và đánh thức tiến trình đang chờ mà không mất sự kiện? | Kiên + Quyền | Timer/wakeup (AL-xx, PA-xx) |
| 5 | ...để Ctrl-C chỉ dừng chương trình foreground, không giết shell? | Kiên | Shell/integration (DL-11) |
| 6 | ...giữ mọi điều trên đúng qua mỗi lần merge của ba người? | Cả nhóm | Regression/SMP (RG-xx) |

**Tiêu chí thành công** (theo mục 1 kế hoạch 8 tuần và quy tắc PASS/FAIL trong `docs/test-plan.md` §1.2):

| Tiêu chí | Cách đo |
|---|---|
| Đủ ba luồng demo bắt buộc | Ctrl-C → SIGINT, `alarm` định kỳ, masked → pending → unmask chạy được trên QEMU (`docs/test-plan.md` §3) |
| Hành vi đúng ngữ nghĩa | Mỗi quy tắc có mã test (DL, HD, MK, AL, PA, REG); test chỉ PASS khi đã chạy thật trên QEMU, log khớp kỳ vọng, không panic |
| Kernel gốc không hỏng | `usertests` đạt `ALL TESTS PASSED` trên `CPUS=1` và `CPUS=4` (RG-01) |
| Kiểm chứng được | Log, ảnh chụp và CI chạy lại được trên chính revision `main` cuối tuần |
| Không nhận stretch là bắt buộc | SIGSEGV kèm fault address và EINTR chỉ ghi "hoàn thành" khi có test ổn định và được xác nhận |

---

## 3. Ideate – Các phương án và lựa chọn

Phần lớn lựa chọn được chốt ở tuần 1 trong `docs/signal-design.md` (quyết định D1–D6). Vài lựa chọn chỉ xuất hiện khi chạy thật ở tuần 3–5. Nhóm chọn cách đơn giản nhất mà vẫn đúng ngữ nghĩa POSIX và kiểm thử được.

| Vấn đề | Phương án đã cân nhắc | Đã chọn | Lý do | Ghi ở |
|---|---|---|---|---|
| Lưu signal chờ xử lý | Hàng đợi đếm số lần; bitmap 1 bit mỗi signal | Bitmap 32-bit `pending_signals` | Đúng POSIX cho signal thường: gửi 5 lần khi chặn, handler chạy 1 lần (DL-04) | D1 |
| SIGKILL | Đi chung đường pending như signal khác; giữ đường gốc của xv6 | Giữ `p->killed = 1` + đánh thức | Không bắt, không chặn được; tận dụng code xv6 đã đúng | D1, D5 |
| Lưu ngữ cảnh khi chạy handler | Một bản `trapframe` trong `struct proc` (kiểu lab alarm MIT); `sigframe` trên user stack | `sigframe` = `saved_tf` + `saved_mask` trên user stack, `sa_restorer` gọi `sigreturn` | Hỗ trợ handler lồng nhiều tầng (HD-11), đúng mô hình Linux; `signum` đi qua `a0`, không lưu trong frame | D3, §3 |
| Khi nào giao signal | Ngay khi `kill`; lúc tiến trình trở về user mode | Lúc trở về user mode, trong `sig_deliver()` | Một điểm duy nhất; không chạy handler trong ngữ cảnh ngắt | Kế hoạch 8 tuần mục 4 |
| Thứ tự khi nhiều signal cùng chờ | Theo thứ tự đến; theo số hiệu tăng dần | Số hiệu tăng dần | Bitmap không nhớ thứ tự đến; là quyết định của nhóm, không phải yêu cầu POSIX (DL-05, HD-13) | `test-plan.md` |
| Kế thừa qua fork/exec | Sao chép hết; sao chép chọn lọc | fork: kế thừa mask và handler, không kế thừa pending và alarm; exec: handler về `SIG_DFL`, giữ `SIG_IGN` | Theo POSIX; test REG-25..28, AL-07 | D6 |
| Khóa cho trường signal | Mọi trường dưới `p->lock`; chia nhóm | `pending_signals`, `alarm_deadline` cần `p->lock`; `blocked_signals`, `sig_actions` là dữ liệu riêng của tiến trình | Ít giữ khóa nhất mà vẫn an toàn khi CPU khác hoặc timer ghi | D6 |
| Timer và deadlock | Lồng `tickslock` và `p->lock`; tách hai bước | `tickslock → p->lock`, không lồng; deadline tuyệt đối so sánh có dấu | Chống tràn `ticks`, độc lập số CPU | §4 |
| `kill()` với signal không được hỗ trợ | Chỉ kiểm tra `1 ≤ sig < NSIG`; kiểm tra thêm `SUPPORTED_MASK` | Trả `-1` ngay đầu `kkill()` khi signal ngoài `SUPPORTED_MASK` | Nhất quán với `sigaction()`; không bật bit pending chết, không đánh thức nhầm (DL-17) | `signal-delivery.md` §3.2 (tuần 5) |
| Alarm qua `exec` | Giữ thời gian còn lại (POSIX); hủy alarm | Hủy alarm (giữ D6); ghi là điểm khác POSIX | Chương trình mới không nhận SIGALRM do chương trình cũ đặt; đơn giản, đã có code. Chốt và thêm test AL-09 ở tuần 6 | D6 (tuần 6) |
| Mất signal khi tiến trình vừa đi ngủ | Khóa toàn cục; kiểm tra signal và đặt `p->chan` trong một lần giữ `p->lock` | `sleep_prepare_intr()` + `sig_send()` xóa `p->chan` | Đóng cả hai khe lost wakeup, không thêm khóa | `signal-core-locking.md` (tuần 3) |
| Ctrl-C tới tiến trình foreground | Process group đầy đủ; một biến `fg_pid` trong console (PA-1) | PA-1: `fg_pid` + syscall `setfg` (27), `sh.c` gọi quanh lệnh foreground | Ít thay đổi nhất; đổi lại chỉ ngắt được con trực tiếp của shell | `signal-mask-alarm.md` §9 (tuần 4) |
| Gọi `kkill()` từ ngắt bàn phím | Hoãn sang luồng kernel; gọi trực tiếp với thứ tự khóa cố định | Gọi trực tiếp, luôn `cons.lock → p->lock` | `kkill()` chỉ dùng spinlock, không `sleep()`; không nơi nào lấy khóa ngược chiều | `signal-core-locking.md` §6 (tuần 4) |
| Chứng minh cho người chấm | Chỉ demo tay; test có mã ID; test tự động trong CI | Mỗi quy tắc trỏ tới đúng một mã test, 3 demo có expected output, toàn bộ test signal chạy tự động trong CI (AU-01..03, tuần 5) | Chạy lại được; mỗi lần merge đều được kiểm tra | `test-plan.md` |

---

## 4. Prototype – Nguyên mẫu theo từng vòng, đối chiếu kế hoạch 8 tuần

Mỗi tuần là một vòng nguyên mẫu: mỗi người làm trên nhánh riêng, gộp vào `main` qua Pull Request có review chéo, rồi cả nhóm chạy lại toàn bộ test trên bản gộp. Bảng dưới đối chiếu trọng tâm trong kế hoạch 8 tuần với việc nhóm thực sự làm, theo commit trên GitHub.

| Tuần | Kế hoạch 8 tuần | Thực tế trên GitHub | Nhận xét |
|---|---|---|---|
| 1 (24–31/10) | Khảo sát mã nguồn, chốt thiết kế, baseline build | Nhập xv6-riscv gốc (PR #1); ba đặc tả `signal-delivery.md`, `signal-handler.md`, `signal-mask-alarm.md` (25/10); đồng bộ đặc tả và dựng khung hạ tầng chung (30–31/10) | Đúng kế hoạch |
| 2 (01–07/11) | Hạ tầng signal và API cơ bản, chưa cần delivery | Toàn bộ 5 syscall đã chạy: `kill` 2 tham số và `sig_deliver` (`56e82c9`), `sigaction`/`sigframe`/`sigreturn` (`f0b5ee7`), `sigprocmask`/`alarm` (`10fb012`), `sigtest` 5 nhóm (`45776af`), `sig_interrupted` + `killtest` (`7315af8`); gộp vào `main` qua PR #4 (05/11) | Đi trước kế hoạch khoảng 3 tuần: gộp trọng tâm tuần 2, 3, 4 và một phần tuần 6 |
| 3 (08/11 trước, thực tế 05–07/11) | Signal core: kill, pending, default action | Sửa lost wakeup hai khe (`7d73a0a`, `8536e3a`); `pausetest` và `alarm_demo`, `mask_demo`; test DL-04/05/09/10, stress `CPUS=4`; `regtest` REG-01..33 kể cả fork/exec (PR #5, #6) | Việc tuần 3 sinh ra từ Test tuần 2, không từ kế hoạch: lỗi đồng thời chỉ thấy khi chạy nhiều CPU |
| 4 (09–15/11) | Signal delivery, user stack, sigreturn | Ctrl-C → SIGINT: `fg_pid`, `setfg`, `sh.c`, Demo 1 (`6110b6d`, merge 15/11); ca biên DL-14..16 (PR #7); nested 3 tầng, tràn stack, dồn dập HD-11..13 (PR #8); lock order `cons.lock → p->lock` (PR #9); CI crash test ổn định (`ad28ce2`) | Trọng tâm tuần 7 của kế hoạch (Ctrl-C, shell) được kéo lên tuần 4; delivery và sigreturn đã xong từ tuần 2 |
| 5 (16–22/11) | sigprocmask, sa_mask, nested | Quyền: `kkill` từ chối signal không hỗ trợ (DL-17), hành vi mặc định 7 signal (DL-18), `sig_deliver` dùng `killed(p)` (PR #10). Hưng: `sa_mask` hoãn signal, mask trong handler, alarm lặp HD-14..16 (REG-37..39), sơ đồ stack nhiều tầng (PR #12). Kiên: script tự động AU-01..03 và bước CI, sửa 2 giới hạn Ctrl-C, mục "Quy tắc signal" trong `signal-design.md` (`feature/autotest-w5`) | Đúng trọng tâm kế hoạch; phần tự động hóa mà kế hoạch đặt ở tuần 7 được kéo lên tuần 5 |
| 6 (23–29/11) | alarm, timer tick, pause | Kế hoạch: SIGALRM từ timer đánh thức tiến trình ngủ (DL-19), stress alarm + kill đa CPU (DL-20), phân tích khóa đường alarm (DOC-02); alarm overrun và alarm xen kẽ SIGUSR1 (HD-17, HD-18); độ chính xác alarm (AL-08), alarm qua `exec` (AL-09), script bắt được FAIL (AU-04), `docs/logs/` | Nội dung lấy từ 3 phát hiện khi đo alarm cuối tuần 5 (mục 5), không chỉ từ kế hoạch |
| 7 (30/11–06/12) | Tích hợp shell, Ctrl-C, stress | Đề xuất: stress đa CPU toàn hệ thống, pexpect/CI đầy đủ, release candidate, danh sách lỗi còn lại | Ctrl-C đã xong; giữ phần stress và chốt release candidate |
| 8 (07–13/12) | Regression, demo, báo cáo | Full regression, diễn tập 3 demo, báo cáo và slide theo checklist mục 6 | Giữ nguyên |

Nguyên tắc rút ra: kế hoạch 8 tuần là khung phân công và mốc nghiệm thu; nội dung thật của mỗi tuần được quyết định bởi mục TODO của báo cáo tuần trước, với sai lệch không quá 25% so với TODO đó.

---

## 5. Test – Kiểm chứng và phát hiện mới

Mọi test chạy trên QEMU với `CPUS=1` và `CPUS=4`, theo quy tắc PASS/FAIL của `docs/test-plan.md` §1.2: đã chạy thật, log khớp kỳ vọng, không panic, ghi theo dạng Mã test → Setup → Action → Expected → Actual → PASS/FAIL.

| Chương trình | Nhóm test trong kế hoạch 8 tuần (mục 5) | Mã test | Kết quả cuối tuần 5 |
|---|---|---|---|
| `killtest` | Signal core | DL-01..18, T1–T17 | 60/60 PASS |
| `regtest` | Handler/return, Mask/nested, fork/exec | HD-01..16, REG-01..39 | REG-01..39 PASS |
| `sigtest` | Mask/nested, Timer | MK-xx, AL-01..07 | ALL PASSED |
| `pausetest` | Timer/wakeup | PA-01..12 | 12/12 PASS |
| `ctrlc_demo`, `alarm_demo`, `mask_demo` | Shell/integration, 3 demo bắt buộc | DL-11, Demo 1–3 | Đạt cả 3 tình huống Ctrl-C |
| `./test-xv6.py signals` | Tự động hóa toàn bộ test signal | AU-01..03 | ALL SIGNAL TESTS PASSED |
| `usertests -q`, CI | Regression/SMP | RG-01, CI `usertests` + `crash` | ALL TESTS PASSED |

**Vòng Test quay lại Define, lần 1 (cuối tuần 4 → tuần 5):** chạy code thật cuối tuần 4 lộ ra 4 vấn đề. Cuối tuần 5 cả 4 đã được xử lý và merge:

| # | Vấn đề tìm ra cuối tuần 4 | Việc tuần 5 | Kết quả |
|---|---|---|---|
| 1 | `kill(getpid(), 5)` và `kill(getpid(), 31)` trả `0` dù hai signal này không được hỗ trợ; bit pending không bao giờ được giao và tiến trình đang ngủ bị đánh thức nhầm | DL-17 | Sửa trong `kkill()`; `killtest` T16 PASS |
| 2 | Hành vi mặc định (D5) mới được kiểm với 3 trong 7 signal được hỗ trợ | DL-18 | `killtest` T17 PASS cả 7 signal |
| 3 | `sa_mask` chưa có test riêng; các test nested đều dùng `sa_mask = 0` | HD-14..16 | `regtest` REG-37..39 PASS |
| 4 | Test signal và 3 demo vẫn chạy tay; CI chỉ chạy `usertests` và `crash` | AU-01..03 | Script và bước CI chạy toàn bộ test signal và Ctrl-C |

**Vòng Test quay lại Define, lần 2 (cuối tuần 5 → tuần 6):** review và một lần đo riêng trên QEMU lộ ra 4 vấn đề mới:

| # | Phát hiện cuối tuần 5 | Cách phát hiện | Việc tuần 6 |
|---|---|---|---|
| 1 | Script tự động chỉ chờ dòng `TEST COMPLETED` của `regtest`, mà dòng này luôn được in kể cả khi có ca FAIL | Review chéo nhánh của Kiên | AU-04 |
| 2 | `alarm(n)` chạy handler đúng sau n tick (n = 1, 5, 10, 50; cả khi chạy bận và khi ngủ; 1 và 4 CPU), nhưng chưa có test nào khẳng định độ chính xác này | Đo bằng `uptime()` | AL-08 |
| 3 | `exec` hủy alarm theo D6, trong khi POSIX giữ thời gian còn lại; chưa có test và chưa ghi là điểm khác POSIX | Chạy thử `alarm(5)` rồi `exec("cat")` | AL-09, chốt D6 |
| 4 | SIGALRM sinh từ timer interrupt (CPU 0) tới tiến trình đang ngủ trong `read`/`wait`/`pause` chưa có test riêng; đường khóa `clockintr → sig_alarm_check` chưa được phân tích | Đối chiếu DL-10 với đường alarm | DL-19, DL-20, DOC-02 |

Hai vòng này cho thấy cùng một quy luật: một bộ kiểm thử tự động cũng phải được kiểm thử, và một hành vi đúng mà không có test khẳng định thì có thể hỏng lúc nào không biết.

**Giới hạn đã biết và ghi trong tài liệu:** Ctrl-C chỉ ngắt tiến trình con trực tiếp của shell (chưa có process group); độ sâu handler lồng nhau bị giới hạn bởi user stack 1 trang (`USERSTACK = 1`, HD-12). Hai giới hạn khác của Ctrl-C (shell không in lại `$` sau Ctrl-C tại dấu nhắc; Ctrl-C bấm trong khe giữa `fork` và `setfg` bị bỏ qua) đã được sửa ở tuần 5. Alarm bị hủy khi `exec` là điểm khác POSIX có chủ đích (D6).

**Stretch chưa nhận là hoàn thành:** SIGSEGV kèm fault address chưa làm; syscall bị ngắt hiện trả `-1` (DL-10) nhưng chưa thống nhất semantics EINTR đầy đủ, nên chưa ghi là xong.

---

## 6. Bài học

* **Test là nơi sinh ra yêu cầu mới.** Việc tuần 3 (lost wakeup), cả 4 việc chính của tuần 5 và phần lớn việc tuần 6 đều đến từ chạy code thật, không có trong kế hoạch 8 tuần.
* **Bản thân script kiểm thử cũng cần được review.** Một bước tự động chỉ chờ dòng "hoàn thành" sẽ báo xanh cả khi test FAIL; review chéo bắt được lỗi này.
* **Đo trước, rồi biến kết quả đo thành test.** Alarm đã chính xác từ tuần 2, nhưng chỉ khi đo mới biết; test AL-08 giữ cho nó đúng qua các lần sửa sau.
* **Đặc tả chung trước, code song song sau.** Tuần 1 mất ba ngày (25/10–01/11) để đồng bộ ba đặc tả riêng thành `signal-design.md` với D1–D6; nhờ đó tuần 2 ba người code song song và gộp được trong một PR.
* **Lỗi đồng thời cần test lặp và nhiều CPU.** Lost wakeup không hiện ra với một lần chạy; phải lặp 500 vòng trên `CPUS=4` mới chứng minh được đã sửa.
* **Chọn phương án nhỏ, ghi rõ giới hạn.** PA-1 cho Ctrl-C và user stack 1 trang đủ cho mục tiêu đề tài; giới hạn được ghi trong tài liệu thay vì giấu đi, đúng checklist "không che giấu lỗi chưa xử lý".
* **Quy trình nhóm là một phần của sản phẩm.** Mỗi người một nhánh, PR có review chéo (Hưng review Quyền, Kiên review Hưng, Quyền review Kiên), merge theo thứ tự Quyền → Hưng → Kiên, và chỉ báo "xong" khi test chạy lại trên chính `main`.
* **Đi trước kế hoạch không có nghĩa là xong sớm.** Thời gian dư ở tuần 6–7 được dùng cho tự động hóa, stress và ổn định, đúng tinh thần mục 3 kế hoạch: không dồn thay đổi kernel lớn đến tuần 7–8.

Tài liệu này được cập nhật cuối mỗi tuần, cùng với báo cáo tuần trong `docs/weekly/`.
