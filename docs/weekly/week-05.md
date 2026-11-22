# Báo cáo tuần 05 — Trần Việt Hưng

*Ngày tổng hợp: 22/11/2026*

## 1. Mục tiêu

Hoàn thiện phần Signal Handler & Context, tập trung vào cơ chế `sa_mask`,
khôi phục signal mask sau `sigreturn()`, kiểm thử xử lý SIGALRM lặp lại
và bảo toàn trạng thái thanh ghi khi xử lý signal.

## 2. Công việc đã thực hiện

### 2.1. Triển khai kiểm thử

Bổ sung các ca kiểm thử trong `user/regtest.c`:

- **REG-37 (HD-14):** Kiểm tra `sa_mask` trì hoãn SIGUSR2 trong khi
  handler SIGUSR1 đang thực thi.
- **REG-38 (HD-15):** Kiểm tra signal mask bên trong handler và xác nhận
  mask ban đầu được khôi phục sau khi handler kết thúc.
- **REG-39 (HD-16):** Kiểm tra SIGALRM tự đăng ký lại đủ 5 lần,
  tính toán không bị sai lệch và bảo toàn các thanh ghi s1-s11.

### 2.2. Cập nhật tài liệu

- Cập nhật `docs/signal-handler.md`: bổ sung sơ đồ nested signal frame,
  trình tự tạo/khôi phục frame và các ca HD-14 đến HD-16.
- Cập nhật `docs/test-plan.md`: bổ sung mô tả và kết quả các ca HD-14,
  HD-15, HD-16.

### 2.3. Git và tích hợp

- Nhánh triển khai: `feature/sa-mask-w5`.
- PR #12: Feature/sa mask w5.
- Hai commit triển khai:
  - `5eb4282`: test: add sa_mask and signal context regression tests.
  - `b70f632`: style: format signal regression tests.
- PR #12 đã được merge vào `main` tại commit `d5ee0d5`.

## 3. Kết quả kiểm thử

### 3.1. Kiểm thử module Handler

Trước khi merge, đã chạy `regtest` và `usertests` trên CPUS=1 và CPUS=4.
Các ca REG-01 đến REG-39 đều PASS; usertests kết thúc với
`ALL TESTS PASSED`.

### 3.2. Kiểm thử tích hợp và tự động hóa trên main

Sau khi tích hợp các thay đổi tuần 5 và cập nhật bộ tự động hóa:

| Chương trình / nhóm kiểm thử | CPUS=1 | CPUS=4 |
|---|---|---|
| sigtest | PASS | PASS |
| pausetest | PASS | PASS |
| killtest | PASS | PASS |
| regtest | PASS | PASS |
| alarm_demo | PASS | PASS |
| mask_demo | PASS | PASS |
| Ctrl+C tại shell | PASS | PASS |
| ctrlc_demo có handler | PASS | PASS |
| ctrlc_demo với SIG_DFL | PASS | PASS |
| usertests | PASS | PASS |

Kết quả tự động hóa AU-01, AU-02, AU-03:
`ALL SIGNAL TESTS PASSED (AU-01, AU-02, AU-03)`.

Kết quả hồi quy usertests:
`ALL TESTS PASSED`.

### 3.3. Định dạng và CI

- Đã chạy `make fmt` và `git diff --exit-code` trước khi hoàn thiện PR.
- GitHub Actions trên commit `ad9a9e8`: hai job `linux` và `macos`
  đều hoàn thành thành công.

## 4. Log kiểm thử đính kèm

Các log hồi quy được lưu trong `docs/logs/`:

- `signals-cpus1.log`
- `signals-cpus4.log`
- `usertests-cpus1.log`
- `usertests-cpus4.log`

## 5. Trạng thái hoàn thành

- Các ca kiểm thử HD-14 đến HD-16 đã được bổ sung.
- Tài liệu Handler và Test Plan đã được cập nhật.
- PR #12 đã được merge vào `main`.
- Kiểm thử tự động AU-01 đến AU-03 đã PASS trên CPUS=1 và CPUS=4.
- Kiểm thử hồi quy usertests đã PASS trên cả hai cấu hình.
- Log kiểm thử được lưu để làm minh chứng.

## 6. Công việc tiếp theo

- Tiếp tục thực hiện nhiệm vụ theo kế hoạch tuần tiếp theo của nhóm.
- Duy trì phối hợp giữa Handler, Signal Core, Mask & Alarm khi có thay đổi
  liên quan đến signal mask, pending signal và quá trình delivery.
- Giữ log kiểm thử và cập nhật Test Plan khi bổ sung ca kiểm thử mới.
