# Property test và fuzzing

UniLume dùng cùng một stateful harness cho test xác định và libFuzzer. Harness
gọi API production, giới hạn input ở 64 KiB và tối đa 4.096 thao tác để mọi
case đều kết thúc trong thời gian hữu hạn.

## Phạm vi

Ba target hiện có:

- `unilume_fuzz_engine`: phím, backspace, reset, đổi Telex/VNI/VIQR và đổi
  options qua `EngineContext`;
- `unilume_fuzz_parsers`: decode/migrate/encode config và macro, kiểm tra
  canonical round-trip;
- `unilume_fuzz_transaction`: focus/reset, navigation (bao gồm thay đổi
  cursor/selection theo adapter contract), mode/options/macro switch, queue,
  timeout và callback stale qua `DirectCommitController` cùng
  `DeterministicBackend`.

Các invariant bắt buộc là UTF-8 hợp lệ, output/queue bị chặn đúng giới hạn,
document khớp model tạo lại từ event log, callback stale/lặp không sửa
document, và cùng input luôn cho cùng kết quả. Property test còn tiêm fault đã
biết để chứng minh bộ dò stale/duplicate hoạt động.

Repository chưa có production parser cho custom keymap, dictionary hoặc IPC.
Vì vậy các bề mặt đó chưa có fuzz target; không dùng legacy file loader hay
parser giả thay thế. Khi các API tương ứng được đưa vào production, phải thêm
target hoặc mở rộng parser target trong cùng issue triển khai API.

## Chạy property test

```sh
cmake -S . -B build/property \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUNILUME_ENABLE_ASAN=ON \
  -DUNILUME_ENABLE_UBSAN=ON
cmake --build build/property --parallel
ctest --test-dir build/property --output-on-failure -R property-model
```

Runner dùng seed PRNG cố định và chạy toàn bộ file trong `fuzz/corpus`. Khi có
regression mới, commit input nhỏ nhất vào corpus của target tương ứng.

## Chạy libFuzzer

Clang là bắt buộc:

```sh
CC=clang CXX=clang++ cmake -S . -B build/fuzz \
  -DCMAKE_BUILD_TYPE=Debug \
  -DUNILUME_BUILD_FUZZERS=ON \
  -DUNILUME_ENABLE_ASAN=ON \
  -DUNILUME_ENABLE_UBSAN=ON
cmake --build build/fuzz --parallel
build/fuzz/fuzz/unilume_fuzz_engine fuzz/corpus/engine \
  -max_len=65536 -timeout=10 -runs=10000
```

Thay `engine` bằng `parsers` hoặc `transaction`. CI pull request chạy campaign
ngắn, hữu hạn; workflow định kỳ chạy campaign dài riêng để không làm chậm PR.

## Replay và minimization

Replay một artifact:

```sh
scripts/fuzz/replay.sh transaction path/to/crash-artifact
```

Minimize trước khi commit:

```sh
build/fuzz/fuzz/unilume_fuzz_transaction \
  -minimize_crash=1 \
  -exact_artifact_path=minimized \
  path/to/crash-artifact
scripts/fuzz/replay.sh transaction minimized
```

Không thêm sanitizer suppression. Một crash chỉ được coi là xử lý xong sau khi
artifact đã minimize, replay được bằng target production và được commit làm
regression seed.
