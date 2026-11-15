# Manifest backup FULL a70q - 2026-11-10T15:48:03Z

Device: SM-A705F/DS XXV, LineageOS 22.2, vendor A11 DXD2
Phuong thuc: dd | base64 qua su (adb wireless), verify size + sha256
Skip (reinstall duoc): system, vendor, product, userdata, cache, hidden, omr, pad

| partition | size (bytes) | sha256 (8 char dau) |
|---|---|---|
| abl | 4194304 | `2b63fb85` |
| aop | 524288 | `1509debe` |
| apdp | 262144 | `00226035` |
| apnhlos | 99614720 | `77649429` |
| bksecapp | 393216 | `d2a3384a` |
| bluetooth | 1048576 | `30e14955` |
| boot | 67108864 | `b6bd1568` |
| bota | 41943040 | `0dc03043` |
| btd | 2097152 | `fc6ad1a5` |
| cmnlib | 524288 | `32f7ea5d` |
| cmnlib64 | 524288 | `7e67c3bb` |
| ddr | 1048576 | `6d5333e3` |
| debug | 10485760 | `a28717be` |
| devcfg | 131072 | `ba272ba0` |
| devinfo | 4096 | `ae8e0b01` |
| dpo | 4096 | `ad7facb2` |
| dqmdbg | 16777216 | `080acf35` |
| dsp | 33554432 | `04a4a4a7` |
| dtbo | 10485760 | `534ee36c` |
| efs | 20971520 | `04d61147` |
| fota | 22020096 | `e5699dfb` |
| fsc | 4096 | `ad7facb2` |
| fsg | 2097152 | `20a298e2` |
| hyp | 1048576 | `8776ce59` |
| keymaster | 524288 | `492a0081` |
| keystore | 524288 | `07854d2f` |
| limits | 4096 | `ad7facb2` |
| logfs | 8388608 | `95d8f7bd` |
| misc | 1048576 | `07e30213` |
| modem | 89128960 | `8261a1f8` |
| modemst1 | 2097152 | `7c0acd01` |
| modemst2 | 2097152 | `e4e32879` |
| msadp | 262144 | `ab8bafc3` |
| param | 12582912 | `4a421025` |
| persist | 33554432 | `ec7a868b` |
| persistent | 524288 | `45e02485` |
| pmic | 524288 | `07854d2f` |
| qupfw | 131072 | `958ce6b9` |
| recovery | 82825216 | `fc034fd4` |
| sec_efs | 20971520 | `0af99afd` |
| secdata | 32768 | `fa9680af` |
| ssd | 8192 | `9f1dcbc3` |
| steady | 4194304 | `e6cd9d40` |
| storsec | 524288 | `a6d3dd21` |
| tz | 4194304 | `3fc8de66` |
| uefisecapp | 2097152 | `fec0912a` |
| vbmeta | 65536 | `e1610785` |
| vk | 2097152 | `c6230c8c` |
| xbl | 4194304 | `4b944562` |
| xbl_config | 4034560 | `3f9e19fb` |

lshal full dump: backups/hals-lshal.txt
MISMATCH: KHONG - tat ca khop 100%
| recovery | 82825216 | `fc034fd4` (sha256 fc034fd4604b8a196a9acf883614b5deee6595dc7bd692bc86c53d4e7366fd5f, LineageOS recovery, 2026-11-15) |
