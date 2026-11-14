# Cài postmarketOS lên Samsung Galaxy A70 (a70q) từ đầu

Hướng dẫn này đưa một chiếc Galaxy A70 đang chạy LineageOS 22.2 sang postmarketOS (giao diện Sxmo),
dùng các package trong repo này. Tình trạng từng tính năng: `README.md`; chi tiết kỹ thuật: `pmos/README.md`;
nguyên nhân các lỗi và cách sửa: `ERRORS.md` (các mã `Cxx` nhắc tới bên dưới).

> **Cảnh báo.** Bước flash **xoá toàn bộ dữ liệu** trong phân vùng USERDATA (ảnh, app, tài khoản...).
> Mới thử trên đúng một máy SM-A705F/DS (bản Việt Nam, XXV). Mở khoá bootloader làm Knox chuyển 0x1
> vĩnh viễn. Bạn tự chịu rủi ro.

Ký hiệu: ✅ đã làm trên máy thật, 🧪 đã chạy lại trên một Docker volume mới (không flash),
⚠️ chưa thử.

## 1. Cần chuẩn bị

**Điện thoại**
- Galaxy A70 SM-A705F, **bootloader đã mở khoá**.
- Đang chạy **LineageOS 22.2** bản không chính thức của rtd1250
  ([rtd1250/a70q_OTA](https://github.com/rtd1250/a70q_OTA), build 20260607), cài trên firmware stock
  Android 11 cuối cùng `A705FXXU5DXD2`, và đã boot được ít nhất một lần. Lý do:
  - pmOS dùng lại firmware của Samsung/Qualcomm trong các phân vùng `vendor`, `apnhlos`, `modem`,
    `persist` (modem, WiFi, Bluetooth, GPU, cảm biến). Repo không chứa firmware đó.
  - Bootloader Samsung từ chối boot image có `os_version`/security patch thấp hơn bản đã boot gần nhất
    (C12), nên boot.img của pmOS chép header từ boot.img của LineageOS.
- Giữ lại file zip LineageOS đã cài (`lineage-22.2-a70q.zip`): cần `boot.img` và `dtbo.img` trong đó.
- Sạc pin trên 50%.

**Máy tính**
- ✅ macOS trên Apple Silicon + Docker Desktop (mọi thứ chạy native arm64 trong container).
  ⚠️ Linux arm64 + Docker nhiều khả năng chạy được nguyên trạng; Linux x86_64 cần qemu-user/binfmt
  cho container arm64 (rất chậm), chưa thử.
- Khoảng 20 GB trống cho Docker volume (pmbootstrap, cache build kernel).
- **heimdall** để flash ở Download Mode:
  - macOS: bản [heimdall-apple-silicon](https://github.com/aljosasavic/heimdall-apple-silicon) v2.2.2
    (sửa lỗi "pipe stalled" của libusb trên Apple Silicon), đặt ở `~/bin/heimdall` hoặc chỉ đường bằng
    biến `HEIMDALL=/đường/dẫn/heimdall`.
  - Linux: gói `heimdall` của bản phân phối.
- Cáp USB-C tốt. Trên Mac mini nên cắm **cổng phía sau**: cổng phía trước từng để lại một thiết bị
  Download Mode "ma" làm heimdall treo (C23).

## 2. Build package và image

```sh
git clone <url repo này> a70q-pmos && cd a70q-pmos

# 1) 6 package của máy: kernel, firmware loader/modem, turnip-kgsl, wlroots có patch, libssc có patch,
#    device package. Lần đầu tự build image Docker a70q-pmb (pmbootstrap 3.12.1) và clone pmaports.
pmos/pmb/run.sh build --arch aarch64 linux-samsung-a70q a70q-qcom-userspace \
    mesa-vulkan-freedreno-kgsl wlroots0.20 libssc device-samsung-a70q

# 2) rootfs + image, giao diện Sxmo, kèm trình duyệt Epiphany và btop.
#    <mật khẩu> là mật khẩu của user "user" (đăng nhập SSH, sudo).
PMB_UI=sxmo-de-sway pmos/pmb/run.sh install --password '<mật khẩu>' --add epiphany,btop
```

🧪 Hai lệnh trên đã chạy lại từ một Docker volume trống (2026-11-14, Mac mini M4): bước 1 mất
khoảng 23 phút (phần lớn là kernel và mesa), bước 2 vài phút. Mọi dữ liệu nằm trong volume
`a70q-pmbwork`; đặt `PMB_VOLUME=<tên>` để dùng volume khác.

**Nếu bước 2 hỏng với `Connection aborted` / `unexpected end of file`** khi tải package: mirror mặc định
của postmarketOS (chuyển hướng theo vị trí) có thể rất chậm từ trong container (lần thử trên chỉ đạt
~15 KB/s tới `mirror.nura.eco`). Chọn mirror khác trong
[danh sách mirror](https://mirrors.postmarketos.org) rồi chạy lại, ví dụ:

```sh
PMB_MIRROR=http://ftp.halifax.rwth-aachen.de/postmarketos/ PMB_UI=sxmo-de-sway \
    pmos/pmb/run.sh install --password '<mật khẩu>' --add epiphany,btop
```

## 3. Chuẩn bị các file flash

```sh
mkdir -p los && unzip -o lineage-22.2-a70q.zip boot.img dtbo.img -d los
pmos/tools/prepare-install.sh --like los/boot.img --dtbo los/dtbo.img
```

🧪 Đã chạy trên kết quả của bước 2 ở volume trống, với `boot.img`/`dtbo.img` lấy từ zip
LineageOS 22.2 20260607: header boot giữ `15.0.0 / 2026-06`, cmdline 430/511 byte (giới hạn 512, C16),
`dtbo.img` trùng từng byte với DTBO đang chạy trên máy thử nghiệm. Script tạo trong `pmos/out/install/`:

| File | Nội dung | Flash vào |
|---|---|---|
| `boot.img` | kernel mới nhất + initramfs và UUID phân vùng của rootfs vừa cài, header chép từ boot LineageOS | BOOT |
| `dtbo.img` | dtbo của LineageOS bỏ connector writeback (thiếu bước này sway không có màn hình, C49) | DTBO |
| `vbmeta.img` | vbmeta rỗng, flags=2 (tắt kiểm tra chữ ký) | VBMETA |
| `userdata.img` | image pmOS (phân vùng boot + root) dạng sparse; heimdall chết ở 7% với image thô (C33) | USERDATA |

## 4. Flash

1. Vào **Download Mode**: tắt máy, giữ **Volume Up + Volume Down** rồi cắm cáp USB vào máy tính,
   bấm **Volume Up** ở màn hình cảnh báo.
2. Flash cả bốn phân vùng trong một lần:

```sh
pmos/tools/flash.sh --BOOT pmos/out/install/boot.img --DTBO pmos/out/install/dtbo.img \
    --VBMETA pmos/out/install/vbmeta.img --USERDATA pmos/out/install/userdata.img
```

`flash.sh` chờ máy vào Download Mode bằng IORegistry (không dùng `heimdall detect`, lệnh đó làm máy
thoát Download Mode, C21), tự thử lại khi lần bắt tay đầu bị treo (C24), log ở `/tmp/a70q-flash-*.log`.
Xong máy tự khởi động lại.

✅ Cách flash này (BOOT, DTBO, VBMETA, USERDATA bằng heimdall với image sparse) là cách đã dùng để cài
máy thử nghiệm.

## 5. Lần khởi động đầu

- Khoảng 15 giây sau logo Samsung, Sxmo hiện lên và tự đăng nhập. Lần đầu rootfs tự giãn ra hết
  USERDATA (~109 GB).
- **SSH qua USB**: máy tạo cổng mạng USB, `ssh user@172.16.42.1` (mật khẩu đặt ở bước 2).
- **WiFi**: menu Sxmo (chuột phải lên thanh trạng thái, hoặc vuốt/nút như mặc định của Sxmo) →
  Networks, hoặc `sudo nmcli device wifi connect '<SSID>' password '<mật khẩu WiFi>'`.
  Modem DSP và WiFi cần khoảng 30 giây sau khi boot mới lên.
- **Nhận file Bluetooth (OBEX)**, bật một lần cho user: `superctl enable a70q-obexd && superctl start a70q-obexd`.

Kiểm tra nhanh qua SSH:

```sh
uname -r                                  # 4.14.190
monitor-sensor                            # gia tốc, ánh sáng, tiệm cận, la bàn (Ctrl+C để thoát)
nmcli device                              # wlan0 connected
bluetoothctl show | grep Powered          # yes
wpctl status | grep -A3 Sinks             # loa/tai nghe
```

## 6. Cập nhật về sau

- Build lại package như bước 2, rồi trên Mac `pmos/tools/repo.sh start` (phục vụ repo package qua HTTP,
  cổng 8099). Trên máy thêm vào đầu `/etc/apk/repositories` dòng do `pmos/tools/repo.sh phone` in ra,
  sau đó `sudo apk update && sudo apk upgrade`. ✅
- Kernel cập nhật qua apk **không** tự ghi phân vùng BOOT: tạo boot.img bằng `pmos/tools/mkboot.sh`
  (xem `pmos/README.md` mục 3) rồi `flash.sh --BOOT`. Có thể đưa máy vào Download Mode từ xa:
  `sudo reboot-mode download` qua SSH. ✅

## 7. Quay về LineageOS

⚠️ Chưa thử trên máy. Flash lại các file gốc trong zip LineageOS rồi xoá dữ liệu bằng recovery của
LineageOS (phân vùng RECOVERY không bị đụng tới khi cài pmOS):

```sh
unzip -o lineage-22.2-a70q.zip vbmeta.img -d los
pmos/tools/flash.sh --BOOT los/boot.img --DTBO los/dtbo.img --VBMETA los/vbmeta.img
```

Sau đó vào recovery (tắt máy, giữ **Volume Up + Power**, cắm USB nếu cần) → Factory reset → Format data.

## 8. Giới hạn đã biết

- Không dùng được: camera, vân tay (cần HAL Android), FM radio (Samsung không bật), NFC (chip không trả
  lời trên SM-A705F, C66), giải mã video phần cứng (C62; CPU giải mã 1080p vẫn dư).
- Chưa làm: gọi điện/SMS/data di động; GPS (engine chạy, chưa thử ngoài trời).
- Epiphany chạy không có sandbox WebKit vì kernel 4.14 thiếu `openat2` (C64).
