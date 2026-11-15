# Phân tích Locking & Đồng bộ hóa đa lõi — Signal Core

* **Dự án:** 26106 — POSIX-style signals trên xv6-riscv
* **Module:** Signal Core (`kill`, `sig_send`, `sig_deliver`, `sig_interrupted`, `sleep_prepare_intr`)
* **Người viết:** Phạm Lê Quyền — Tuần 3
* **Phạm vi code:** `main@9880d92` + thay đổi Tuần 3 của nhánh `feature/signal-core-w3`

---

## 1. Ai đọc / ghi trường nào

Mấu chốt của thiết kế: **chỉ `pending_signals`, `killed`, `chan`, `state` và `alarm_deadline` bị CPU khác chạm vào**. Các trường còn lại chỉ do chính tiến trình sở hữu đọc/ghi (trong syscall hoặc trên đường trở về user của chính nó).

| Trường trong `struct proc` | Ghi bởi | Đọc bởi | Bị CPU khác truy cập? | Bảo vệ |
|---|---|---|---|---|
| `pending_signals` | `sig_send()` (người gửi, CPU bất kỳ); `sig_deliver()` xóa bit (chủ) | `sig_deliver()`, `sig_deliverable_locked()` (chủ) | **Có** | `p->lock` ở mọi chỗ ✅ |
| `killed` | `kkill()` khi `SIGKILL`, `setkilled()` | `killed()`, `sig_deliverable_locked()`, `sig_deliver()` | **Có** | `p->lock` ✅ (trừ 1 chỗ, xem §4.2) |
| `chan` | `sleep_prepare()`, `sleep_prepare_intr()` (chủ); `wakeup()`, `sig_send()`, `kkill()` xóa về 0 (CPU bất kỳ) | `sleep()`, `wakeup()` | **Có** | `p->lock` ✅ |
| `state` (SLEEPING → RUNNABLE) | `sig_send()`, `kkill()`, `wakeup()`, scheduler | scheduler, `sleep()` | **Có** | `p->lock` ✅ |
| `alarm_deadline` | `sys_alarm()` (chủ); `sig_alarm_check()` (timer, CPU bất kỳ) | như trên | **Có** | `p->lock` ✅ |
| `blocked_signals` | `sys_sigprocmask()`, `sig_setup_frame()`, `sys_sigreturn()` (đều là chủ) | `sig_deliver()`, `sig_deliverable_locked()`, `kfork()` (đều là chủ) | Không | Không cần khóa — chỉ một luồng thực thi |
| `sig_actions[]` | `sys_sigaction()`, `kexec()` (chủ) | `sig_deliver()`, `sig_deliverable_locked()`, `kfork()` (chủ) | Không | Không cần khóa |

**Kết luận:** mọi trường bị chia sẻ giữa các CPU đều được ghi/đọc dưới `p->lock`. `blocked_signals` và `sig_actions[]` không có tranh chấp vì một tiến trình chỉ chạy trên một CPU tại một thời điểm và không ai khác chạm vào chúng.

> ⚠️ Comment trong `kernel/proc.h` ghi "p->lock must be held when using these" cho cả nhóm trường signal — **không khớp với code thực tế** (`sigprocmask`, `sig_setup_frame`, `sigreturn` ghi `blocked_signals` không khóa). Code đúng; comment nên sửa lại theo bảng trên.

---

## 2. Tính nguyên tử khi gửi signal

```c
// kkill(): giữ p->lock suốt quá trình tìm + ghi
acquire(&p->lock);
if (p->pid == pid && p->state != UNUSED && p->state != ZOMBIE) {
    sig_send(p, sig);   // pending |= bit; p->chan = 0; SLEEPING -> RUNNABLE
    release(&p->lock);
```

* `pending_signals |= bit` là read-modify-write; nếu hai CPU cùng gửi mà không khóa có thể **mất bit**. Vì cả `sig_send()` lẫn chỗ xóa bit trong `sig_deliver()` đều nằm trong `p->lock`, không bit nào bị mất (kiểm chứng: Test 11 — 4 CPU × 200 `kill` đồng thời).
* Kiểm tra `pid`/`state` cùng vùng khóa với việc ghi bit ⇒ không có cửa sổ "pid vừa bị tái sử dụng / tiến trình vừa thành ZOMBIE".
* **Gộp signal (coalescing):** `pending_signals` là bitmask nên gửi N lần cùng một signal khi đang bị chặn chỉ để lại 1 bit ⇒ handler chạy đúng 1 lần (Test 7).

---

## 3. Thứ tự khóa (chống deadlock)

Mọi chỗ giữ hai khóa cùng lúc đều theo **cùng một chiều: khóa điều kiện → `p->lock`**. Không có chỗ nào lấy `p->lock` rồi mới lấy khóa khác.

| Đường code | Thứ tự |
|---|---|
| `piperead` / `pipewrite` → `sig_interrupted()`, `sleep_prepare_intr()` | `pi->lock` → `p->lock` |
| `consoleread` → `sig_interrupted()`, `sleep_prepare_intr()` | `cons.lock` → `p->lock` |
| `kwait` → `sig_interrupted()`, `sleep_prepare_intr()` | `wait_lock` → `p->lock` |
| `sys_pause` → `sig_interrupted()`, `sleep_prepare_intr()` | `tickslock` → `p->lock` |
| `sleep_prepare()` (gốc), `wakeup()` | khóa điều kiện → `p->lock` |
| `sleep()` | chỉ `p->lock` (khóa điều kiện đã được nhả trước đó) |
| `clockintr` → `sig_alarm_check` | `tickslock` được **nhả trước**, rồi mới lấy từng `p->lock` |
| `sys_alarm` | đọc `ticks` dưới `tickslock`, **nhả**, rồi lấy `p->lock` |
| `kkill` | chỉ giữ 1 `p->lock` tại một thời điểm |

`kfork()` cố ý **không** lấy `p->lock` của cha khi đang giữ `np->lock`; việc sao chép `blocked_signals`/`sig_actions` của cha an toàn vì đó là trường riêng của chính tiến trình đang gọi `fork` (§1).

---

## 4. Lost wakeup & các rủi ro còn lại

### 4.1. Lost wakeup trong vòng chờ — đã đóng cả hai khe (Tuần 3)

Bản xv6 này ngủ theo hai bước: `sleep_prepare(chan)` đặt `p->chan`, nhả khóa điều kiện, rồi `sleep()` chỉ ngủ nếu `p->chan != 0`. `wakeup()` báo sự kiện bằng cách xóa `p->chan = 0`. Vòng chờ có signal:

```
(1) sig_interrupted(p) == 0 ?     -- dưới khóa điều kiện
(2) sleep_prepare(chan)           -- p->chan = chan
    release(khóa điều kiện)
(3) sleep()                       -- ngủ nếu p->chan != 0
```

| Khe | Hiện tượng | Cách đóng |
|---|---|---|
| Signal đến giữa (2) và (3) | Tiến trình đang `RUNNING`, `p->chan` ≠ 0 ⇒ ngủ dù có signal | `sig_send()`/`kkill()` xóa `p->chan = 0` (commit `7d73a0a`) |
| Signal đến giữa (1) và (2) | `sig_send()` xóa `p->chan` nhưng (2) gán lại ⇒ ngủ dù có signal | `sleep_prepare_intr(chan)`: kiểm tra signal **và** đặt `p->chan` trong **cùng một lần giữ `p->lock`** (Tuần 3) |

Vì `sig_send()` cũng cần `p->lock`, nó chỉ có thể chạy **trước** bước nguyên tử (bị phát hiện ⇒ không đặt `chan`, vòng lặp trả `-1`) hoặc **sau** (xóa `chan` ⇒ `sleep()` không ngủ). Không còn khe.

**Kiểm chứng bằng chèn trễ (fault injection)** giữa (1) và (2) trong `piperead`, 300 vòng trên `CPUS=4`:

| Kernel (có chèn trễ) | Mất wakeup |
|---|---|
| Chỉ có `p->chan = 0` | 7 / 300 |
| + `sleep_prepare_intr()` | 0 / 300 |

### 4.2. `sig_deliver()` đọc `p->killed` không khóa — rủi ro thấp
```c
if (p->killed)      // kernel/trap.c, đọc trần
    kexit(-1);
```
`killed` có thể bị CPU khác ghi cùng lúc. Trên RISC-V phép đọc `int` căn lề là nguyên tử nên không sai kết quả (cùng lắm trễ một lần trả về user), nhưng về hình thức là data race. **Đề xuất:** đổi thành `if (killed(p))`.

### 4.3. Comment trong `proc.h` (xem §1)
Đề xuất: *"`pending_signals`, `alarm_deadline`, `chan`: cần `p->lock`. `blocked_signals`, `sig_actions`: chỉ tiến trình sở hữu truy cập, không cần khóa."*

---

## 5. Bằng chứng kiểm thử

`user/killtest.c` — 12 nhóm / 38 kiểm tra (đánh số theo `week-03-plan`):

| Test | Nội dung | Mã |
|---|---|---|
| 1–2 | `kill` với sig / pid không hợp lệ trả `-1` | DL-07, DL-06 |
| 3–4 | `SIGKILL` kết thúc tiến trình đang chạy / đang ngủ | DL-10 |
| 5–6 | `SIG_DFL` kết thúc tiến trình; `SIGTERM` đánh thức tiến trình ngủ trên `read` | DL-08, DL-10 |
| 7 | Gửi 5 `SIGUSR1` khi bị chặn (từ chính mình và từ tiến trình khác) → mở chặn, handler chạy **đúng 1 lần** | DL-04 |
| 8 | Chặn cả hai, gửi `SIGUSR2` rồi `SIGUSR1`, mở chặn → `SIGUSR1` chạy trước | DL-05 |
| 9 | `SIG_IGN`: tiến trình không bị kết thúc, `read()` đang chờ không bị ngắt | DL-09 |
| 10 | Signal có handler ngắt `read()`: trả `-1`, handler chạy, tiến trình sống; 500 vòng gửi đúng lúc vào `read()` → **0 lần mất wakeup** | DL-10 |
| 11 | 4 tiến trình × 200 `kill` đồng thời → không mất bit; 4 tiến trình cùng `SIGKILL` 4 nạn nhân → không panic/deadlock | DL-13 |
| 12 | Signal đang bị chặn **không** ngắt `read()` | DL-12 |

| Cấu hình | `killtest` | `sigtest` | `pausetest` | `forktest` | `usertests -q` |
|---|---|---|---|---|---|
| `CPUS=1` | 38/38 PASS × 3 lần | ALL PASSED | 12/12 | OK | ALL TESTS PASSED |
| `CPUS=4` | 38/38 PASS × 3 lần | ALL PASSED | 12/12 | OK | ALL TESTS PASSED |

---

## 6. Lock order cho Ctrl-C → SIGINT (Tuần 4)

Rà soát nhánh `feature/ctrlc-sigint` (commit `6110b6d`, Kiên). Đường đi khi người dùng bấm Ctrl-C:

```
UART interrupt → devintr() → uartintr() → consoleintr(0x03)
    acquire(&cons.lock)
    in "^C", bỏ dòng đang gõ dở (cons.e = cons.w)
    if (fg_pid > 0) kkill(fg_pid, SIGINT)
        └─ với từng proc: acquire(&p->lock) … sig_send() … release(&p->lock)
    release(&cons.lock)
```

### 6.1. Mọi chỗ lấy `cons.lock`

| Hàm | Đang giữ khóa nào trước khi lấy `cons.lock` | Lấy thêm khóa nào khi đang giữ `cons.lock` |
|---|---|---|
| `consoleintr()` (ngắt UART) | không | `p->lock` (qua `kkill` → `sig_send`) |
| `consoleread()` (syscall `read`) | không | `p->lock` (qua `sig_interrupted`, `sleep_prepare_intr`) |
| `console_setfg()` (syscall `setfg`) | không | không |

Không có đường code nào **đang giữ `p->lock` rồi mới lấy `cons.lock`**: `sig_send()`, `sleep()`, `sched()`, `procdump()` đều không đụng tới `cons.lock`. Vì vậy thứ tự khóa trong toàn hệ thống luôn là **`cons.lock → p->lock`**, cùng chiều với các khóa điều kiện khác ở §3 ⇒ **không thể xảy ra deadlock** kiểu hai luồng giữ hai khóa theo thứ tự ngược nhau (ví dụ `wait(S); wait(Q)` / `wait(Q); wait(S)` trong slide Chương 2).

### 6.2. Gọi `kkill()` từ ngữ cảnh ngắt có an toàn không

* `kkill()` và `sig_send()` **chỉ dùng spinlock** (`p->lock`) và **không gọi `sleep()`**: chỉ ghi bit pending, xóa `p->chan`, đổi `SLEEPING → RUNNABLE`. Đây đúng là các thao tác được phép làm trong trình xử lý ngắt.
* **Cùng một CPU:** `acquire()` gọi `push_off()` tắt ngắt trên CPU đó. Khi một CPU đang giữ `p->lock` (trong syscall `kill`, trong `sleep()`…), ngắt UART không thể chen vào **trên chính CPU đó** để gọi `consoleintr()` → không có chuyện CPU tự chờ khóa của chính mình.
* **Khác CPU:** CPU A đang chạy `consoleintr()` giữ `cons.lock` và chờ `p->lock` mà CPU B đang giữ. CPU B chỉ giữ `p->lock` trong các đoạn ngắn không bao giờ chờ `cons.lock` (§6.1) → B nhả khóa, A chạy tiếp.
* `consoleintr()` giữ `cons.lock` trong lúc `kkill()` duyệt bảng tiến trình (tối đa `NPROC` = 64 lần `acquire/release`). Thời gian giữ khóa ngắn, chấp nhận được — giống `procdump()` (Ctrl-P) vốn cũng duyệt bảng tiến trình khi giữ `cons.lock`.

### 6.3. Tương tác với tiến trình foreground đang ngủ

* Tiến trình foreground đang chờ trong `consoleread()`, `pause()`, `wait()`, pipe: `sig_send()` xóa `p->chan` và đánh thức; vòng chờ dùng `sleep_prepare_intr()` nên **không mất wakeup** (§4.1) → syscall trả `-1`, `sig_deliver()` giao `SIGINT` (chạy handler hoặc `SIG_DFL` kết thúc).
* `fg_pid` trỏ tới tiến trình đã thoát (giữa lúc con `exit` và lúc shell gọi `setfg(0)`): `kkill()` bỏ qua `UNUSED`/`ZOMBIE` và trả `-1`, không ảnh hưởng gì.

### 6.4. Giới hạn đã biết (không phải lỗi đồng bộ)

* **Khe trước `setfg(pid)`:** `sh.c` gọi `setfg(pid)` sau `fork1()`; nếu Ctrl-C được bấm trong khoảng rất ngắn trước lời gọi này thì `fg_pid` còn là 0 và phím bị bỏ qua.
* **Pipeline / lệnh lồng:** `fg_pid` là tiến trình con trực tiếp của shell; các tiến trình cháu (ví dụ `a | b`) không nhận `SIGINT` (không làm process group – phương án PA-2 để dành cho stretch).
* **Ctrl-C tại dấu nhắc:** shell không bị kết thúc (đúng yêu cầu), dòng đang gõ bị bỏ, nhưng shell không in lại dấu nhắc `$` mới cho tới khi người dùng gõ lệnh tiếp.

### 6.5. Bằng chứng kiểm thử (bản gộp `feature/signal-edge-w4` + `feature/ctrlc-sigint`)

| Tình huống DL-11 | `CPUS=1` | `CPUS=4` |
|---|---|---|
| Ctrl-C tại dấu nhắc shell → shell vẫn sống, nhận lệnh tiếp | PASS | PASS |
| `ctrlc_demo` (có handler) → handler chạy, in thông báo, thoát, về dấu nhắc | PASS | PASS |
| `ctrlc_demo dfl` (`SIG_DFL`) → tiến trình kết thúc, về dấu nhắc | PASS | PASS |

Cùng bản gộp: `killtest` 47/47 PASS (`CPUS=1` và `CPUS=4`), `usertests -q` ALL TESTS PASSED (`CPUS=4`).