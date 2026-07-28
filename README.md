# UniLume

**Bộ gõ tiếng Việt hiện đại, nhẹ và nhanh cho Linux, phát triển trực tiếp từ lõi UniKey.**

UniLume đang ở giai đoạn thử nghiệm. Repository cung cấp lõi xử lý tiếng Việt
kế thừa, API C theo từng input context, bộ điều khiển direct-commit C++23,
harness integration xác định và một addon Fcitx5 MVP tùy chọn. Addon chưa được
coi là sẵn sàng để sử dụng hằng ngày.

> UniLume là một dự án độc lập, được phát triển dựa trên UniKey Input Engine của Phạm Kim Long. UniLume không phải sản phẩm chính thức của UniKey và không được Phạm Kim Long đại diện hoặc bảo trợ.

## Hiện có

- Lõi UniKey/x-unikey 1.0.4 cho Telex, VNI và VIQR.
- Xuất UTF-8 và các bộ chuyển đổi bảng mã kế thừa.
- API xử lý phím, backspace, macro và keymap của x-unikey.
- Macro UTF-8 production có validation, import VIQR, persistence nguyên tử và
  isolation theo input context.
- Build CMake cho thư viện lõi, regression test và integration test trên Linux.
- Harness mô phỏng surrounding text trễ/lỗi và benchmark backlog/RSS tùy chọn.
- Addon Fcitx5 Telex/UTF-8 thử nghiệm với direct-commit khi surrounding text
  đáng tin và fallback an toàn cho frontend bất đồng bộ, mặc định không build.
- GUI cấu hình Qt6 tùy chọn cho toàn bộ option production, editor tài nguyên,
  validation, backup/restore có phiên bản và apply nguyên tử qua Fcitx D-Bus.
- Mã adapter XIM/GTK2 lịch sử trong `src/platform/legacy/` để tham khảo; các
  adapter này chưa nằm trong build mặc định.

## Chưa có

- Gói distro hoặc uinput fallback.
- Xác minh trên Wayland và ma trận ứng dụng/phân phối rộng hơn môi trường
  KDE/X11 đã kiểm tra.
- Backend IBus hoặc tích hợp Wayland cấp hệ thống độc lập.
- Cam kết tương thích, ổn định hay sẵn sàng cho production.

## Build và test

Luồng phát triển core dùng [Cippie](https://github.com/dismonjames/cippie) (đã kiểm tra với 0.1.6) và compiler C++23
trên Linux. Cippie không cần CMake để build hoặc chạy test core, và build mặc
định không tải dependency từ Internet.

```sh
cippie build --offline
cippie test --offline
```

Lệnh trên tạo thư viện core; `cippie test` cũng build và chạy engine lẫn
integration test. CMake 3.16 trở lên vẫn được giữ cho các luồng chưa do Cippie
quản lý: sanitizer, benchmark, addon Fcitx5 và install/package.

Để chạy ASan và UBSan cục bộ qua CMake:

```sh
cmake -S . -B build/sanitizers \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUNILUME_ENABLE_ASAN=ON \
  -DUNILUME_ENABLE_UBSAN=ON
cmake --build build/sanitizers
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ctest --test-dir build/sanitizers --output-on-failure
```

Các option sanitizer mặc định tắt và không thay thế test build thông thường.
ThreadSanitizer dùng một build riêng vì không thể kết hợp với ASan:

```sh
cmake -S . -B build/tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUNILUME_ENABLE_TSAN=ON
cmake --build build/tsan
TSAN_OPTIONS=halt_on_error=1 \
ctest --test-dir build/tsan --output-on-failure
```

Addon Fcitx5 vẫn chỉ được bật với
`-DUNILUME_BUILD_FCITX5_ADDON=ON`; xem
[docs/fcitx5-addon.md](docs/fcitx5-addon.md).
GUI cấu hình được bật thêm với `-DUNILUME_BUILD_CONFIG_GUI=ON`; xem
[docs/configuration-gui.md](docs/configuration-gui.md).

Benchmark core được tắt mặc định. Cách build Release, chạy corpus, xuất JSON
và chạy soak được mô tả trong
[docs/benchmarks.md](docs/benchmarks.md). Các số core-only không phải phép so
sánh với Lotus, fcitx5-unikey hoặc một integration desktop hoàn chỉnh.

Protocol và harness X11 để so sánh công bằng với Fcitx5 Lotus được mô tả trong
[docs/lotus-comparison.md](docs/lotus-comparison.md). Chỉ kết quả chạy theo
protocol này mới có thể hỗ trợ một tuyên bố so sánh; benchmark core không đủ.

Cách chạy harness delayed/stale, burst, soak và benchmark integration được mô
tả trong [docs/integration-testing.md](docs/integration-testing.md).
Property test, corpus và libFuzzer được mô tả trong
[docs/fuzz-testing.md](docs/fuzz-testing.md).
Contract custom keymap và quy tắc activate/rollback được mô tả trong
[docs/keymap-support.md](docs/keymap-support.md).
Personal dictionary và policy keep/restore được mô tả trong
[docs/dictionary-support.md](docs/dictionary-support.md).
Policy theo ứng dụng, bốn mode, menu trạng thái và hotkey được mô tả trong
[docs/application-policy.md](docs/application-policy.md).
Contract verified direct replacement, feature flag và rollback được mô tả
trong [docs/verified-direct-backend.md](docs/verified-direct-backend.md).
Pipeline tiện ích gõ tùy chọn, thứ tự transform và reset contract được mô tả
trong
[docs/adr/0003-typing-convenience-pipeline.md](docs/adr/0003-typing-convenience-pipeline.md).
Diagnostic production có giới hạn, inventory riêng tư và cách thu thập bug
report được mô tả trong [docs/diagnostics.md](docs/diagnostics.md).
Kết quả cài user-local và kiểm tra ứng dụng desktop thực tế được ghi tại
[docs/real-application-validation.md](docs/real-application-validation.md).

Chính sách lựa chọn input path cho browser và phân tích zero-preedit được mô
tả trong [docs/browser-input-policy.md](docs/browser-input-policy.md).

Nghiên cứu composition span / stable-prefix (quyết định C cho mô hình
commit-only, không bật hybrid) trong
[docs/composition-span-research.md](docs/composition-span-research.md) và
[docs/stable-prefix.md](docs/stable-prefix.md).

Xác thực Wayland, harness qualification tự động cho họ wlroots và checklist
kiểm tra thủ công cho KWin/Mutter trong
[docs/wayland-validation.md](docs/wayland-validation.md).

## Nguồn gốc và giấy phép

Phần lớn code trong `src/` và tài liệu trong `docs/legacy/` đến từ x-unikey
1.0.4/UniKey Input Engine của Phạm Kim Long. `third_party/imdkit/` là IMdkit của
X11R6 với các notice permissive riêng. Các file CMake, test và tài liệu
UniLume ở cấp repository là phần mới.

Target build hiện tại kết hợp code GPL-2.0-or-later và LGPL-2.0-or-later, nên
được phân phối theo GPL-2.0-or-later. Một số file kế thừa không có header riêng
và `COPYING` dùng tên lịch sử “GNU Library General Public License, version 2”;
xem [NOTICE](NOTICE) và [docs/licensing.md](docs/licensing.md) trước khi phát
hành binary.

`LICENSE` chứa GPL version 2; `COPYING` giữ nguyên văn bản giấy phép đi kèm
snapshot gốc. Mọi header bản quyền và điều khoản riêng của bên thứ ba vẫn có
hiệu lực.

## Ghi công

### Nguồn gốc & Dự án kế thừa

- Phạm Kim Long — tác giả UniKey và UniKey Input Engine.
- Nhóm x-unikey cùng các cộng tác viên được liệt kê trong [AUTHORS.md](AUTHORS.md).
- Hidetoshi Tajima và X11R6 Xi18n Implementation Group — IMdkit.

UniKey là tên của dự án gốc và chỉ được dùng ở đây để ghi công, mô tả nguồn gốc kỹ thuật hoặc giữ tương thích API.

### Đội ngũ phát triển UniLume (Developers)

- [dismonjames](https://github.com/dismonjames) — Lead Developer
- [hnrie](https://github.com/hnrie) — Developer

## Roadmap ngắn

1. Xác minh provenance và giấy phép các file kế thừa chưa có header.
2. Mở rộng test hồi quy mà không đổi thuật toán.
3. Thiết kế adapter Linux nhỏ, tách biệt khỏi engine.
4. Chỉ sau đó đánh giá backend desktop và packaging.

Chi tiết xem [docs/roadmap.md](docs/roadmap.md).

## Ủng hộ dự án (Donate)

Nếu UniLume mang lại giá trị cho bạn, bạn có thể ủng hộ nhà phát triển để duy trì và hoàn thiện dự án thông qua mã QR bên dưới:

<p align="center">
  <img src="img/ungho.png" alt="Mã QR ủng hộ UniLume" width="320" />
  <br>
  <sub><i>Cảm ơn sự hỗ trợ và đồng hành của bạn dành cho UniLume! ❤️</i></sub>
</p>

### Bảng vinh danh nhà ủng hộ (Supporters)

*Chưa có lượt ủng hộ nào. Hãy là người đầu tiên đóng góp và xuất hiện tại đây!*

<!-- Cập nhật danh sách nhà ủng hộ tại đây -->


