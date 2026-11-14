# Userspace cho modem/WLAN trên a70q (downstream 4.14)

`a70q-modem-up.sh` — khởi động chuỗi dịch vụ Qualcomm và boot modem (xem ERRORS.md mục I).
Binary tự build tĩnh (Alpine arm64, musl):

| Tool | Nguồn | Patch |
|---|---|---|
| qrtr-ns, qrtr-lookup | linux-msm/qrtr @ 6d950d2 (trước commit "ns: Drop qrtr-ns") | `patches/qrtr-ns-0001-trust-configured-node.patch` (phòng hờ, không phải nguyên nhân) |
| pd-mapper | linux-msm/pd-mapper @ 5ecd2fe | `patches/pd-mapper-0001-PD_MAPPER_JSON_DIRS.patch` |
| tqftpserv | linux-msm/tqftpserv @ b31de6d | `patches/tqftpserv-0001-fallback-without-remoteproc.patch` |
| rmtfs | Alpine edge `rmtfs-1.3` (apk) | — (dùng `-P -r`, cần `/dev/qcom_rmtfs_uio1 -> /dev/uio0`) |

Build (ví dụ pd-mapper):
`gcc -static -O2 -I<qrtr>/include -o pd-mapper pd-mapper.c assoc.c json.c lzma_decomp.c servreg_loc.c <qrtr>/lib/*.c -llzma`
