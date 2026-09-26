# PX68K Baremetal for Raspberry Pi (Circle)

`x68k-cicle`는 Sharp X68000 에뮬레이터 코어(`px68k-libretro`)를 `blueberry-msx`와 동일한 방식으로 [Circle C++ 베어메탈 프레임워크](https://github.com/rsta2/circle) 기반으로 포팅하여, 리눅스 OS 없이 라즈베리파이 하드웨어에서 직접 부팅 및 실행되는 초고속 베어메탈 에뮬레이션 시스템입니다.

---

## 1. 아키텍처 개요

- **에뮬레이터 코어**: `px68k-libretro` (Motorola 68000 CPU 에뮬레이션 `c68k`, FM/OPM/PSG 사운드 생성기 `fmgen`, CRTC/GVRAM/TVRAM, SASI/SCSI/FDD 컨트롤러)
- **베어메탈 플랫폼**: Circle (Raspberry Pi 1, 2, 3, 4 지원)
- **화면 출력 (`CircleVideo`)**: Circle `CScreenDevice`를 통한 HDMI 프레임버퍼 (기본 800x600 16bpp RGB565), X68000 해상도(768x512 / 512x512 / 640x480 등) 중앙 정렬 및 캐시 동기화
- **오디오 출력 (`CircleSound`)**: Circle `CVCHIQSoundBaseDevice`를 통한 HDMI / 3.5mm 아날로그 출력 (44,100Hz 16-bit 스테레오 PCM, 저지연 큐 버퍼링)
- **입력 처리 (`CircleInput`)**:
  - **USB 키보드**: Circle `CUSBKeyboardDevice` (HID scancode -> libretro RETROK_* 키 매핑)
  - **USB 게임패드**: Circle `CUSBGamePadDevice` (표준 버튼 및 D-Pad / 아날로그 스틱 매핑)
  - **USB 마우스**: Circle `CMouseDevice` (마우스 X/Y 이동 및 좌/우 클릭 지원)
- **스토리지 & 파일 I/O (`stub.cpp`)**:
  - Circle `CEMMCDevice` + `FatFs` 라이브러리로 FAT32 SD 카드 마운트 (`SD:`)
  - Newlib C 런타임 시스템 콜(`_open`, `_read`, `_write`, `_lseek`, `_close`, `_stat`, `_sbrk`, `opendir`, `readdir` 등)을 FatFs에 직접 브릿지

---

## 2. 디렉터리 구성

```
x68k-cicle/
├── Circle/                    # Circle 베어메탈 어댑터 레이어
│   ├── CircleKernel.h/cpp     # Circle 커널 클래스 (장치 초기화 및 장치 관리)
│   ├── CircleVideo.h/cpp      # 화면 프레임버퍼 렌더러
│   ├── CircleSound.h/cpp      # VCHIQ 사운드 디바이스 버퍼 큐
│   ├── CircleInput.h/cpp      # USB 키보드, 게임패드, 마우스 입력
│   ├── CircleMain.cpp         # libretro 브릿지 및 메인 에뮬레이션 루프
│   ├── stub.cpp               # FatFs 시스템 콜 & C 런타임 스텁
│   ├── webx68k_baremetal.c    # WebX68k 훅 및 _sbrk 메모리 할당 스텁
│   └── dirent.h               # 디렉터리 탐색용 POSIX 헤더
├── px68k-libretro/            # X68000 에뮬레이터 코어 소스
├── dist_baremetal/            # 최종 배포 패키지 디렉터리
│   ├── kernel.img / app.bin      # Raspberry Pi 1 / Zero용 커널
│   ├── kernel7.img / app7.bin    # Raspberry Pi 2용 커널
│   ├── kernel8-32.img / app8-32.bin # Raspberry Pi 3용 커널 (32-bit)
│   ├── kernel7l.img / app7l.bin  # Raspberry Pi 4 / 400용 커널
│   ├── config.txt / cmdline.txt  # 라즈베리파이 부팅 설정
│   ├── human302.xdf              # Human68k v3.02 시스템 부팅 디스크
│   └── keropi/
│       ├── iplrom.dat            # X68000 IPL BIOS ROM
│       └── cgrom.dat             # X68000 Font CGROM
├── Makefile                   # 빌드 메이크파일 (RASPPI=1,2,3,4 지원)
├── build_all_baremetal.sh     # 전체 라즈베리파이 타겟 자동 빌드 스크립트
└── copy_to_sd.sh              # SD 카드 원클릭 배포 스크립트
```

---

## 3. 빌드 방법

### 개별 타겟 빌드
```bash
# Raspberry Pi 1 / Zero
make RASPPI=1

# Raspberry Pi 2
make RASPPI=2

# Raspberry Pi 3
make RASPPI=3

# Raspberry Pi 4 / 400
make RASPPI=4
```

### 전체 타겟 일괄 빌드
```bash
./build_all_baremetal.sh
```

---

## 4. PC용 바이너리 빌드 (Linux & Windows)

베어메탈 라즈베리파이 외에도 개발 및 테스트 편의를 위해 SDL2 기반의 네이티브 실행 파일 생성을 지원합니다:

### Linux 네이티브 빌드 (`x68k.linux`)
```bash
make linux
# 또는
make x68k.linux
# 실행
./x68k.linux [디스크이미지.xdf]
```

### Windows 실행 파일 빌드 (`x68k.window.exe`)
MinGW-w64 (`i686-w64-mingw32-gcc`) 및 SDL2 MinGW 라이브러리를 사용하여 크로스 컴파일됩니다:
```bash
make windows
# 또는
make x68k.window.exe
# 빌드 결과물: x68k.window.exe, SDL2.dll
```

### PC 조작 키 및 단축키
- **방향키 / Enter**: 게임패드 1P 십자키 / Start
- **Z, X / A, S**: 게임패드 1P 버튼 B, A / Y, X
- **F10**: 전체화면 전환 (Toggle Fullscreen)
- **F11 / 마우스 휠 클릭**: 마우스 캡처 모드 전환 (Mouse Grab)

---

## 5. SD 카드 설치 및 부팅 방법

1. FAT32로 포맷된 SD 카드를 준비합니다.
2. Circle의 기본 펌웨어 파일(`bootcode.bin`, `start.elf` 또는 `start4.elf`, `fixup.dat` 등)이 SD 카드 루트에 위치해야 합니다.
3. `copy_to_sd.sh` 스크립트를 실행하거나 `dist_baremetal/` 폴더 안의 모든 파일을 SD 카드 루트로 복사합니다:
   ```bash
   ./copy_to_sd.sh /media/$USER/BOOT
   ```
4. SD 카드 루트 파일 구조 예시:
   ```
   SD:/
   ├── bootcode.bin
   ├── start.elf
   ├── fixup.dat
   ├── config.txt
   ├── cmdline.txt
   ├── kernel.img       (Pi 1)
   ├── kernel7.img      (Pi 2)
   ├── kernel8-32.img   (Pi 3)
   ├── kernel7l.img     (Pi 4)
   ├── human302.xdf     (시스템 디스크)
   └── keropi/
       ├── iplrom.dat   (BIOS)
       └── cgrom.dat    (Font ROM)
   ```
5. 라즈베리파이에 SD 카드를 삽입하고 전원을 켜면 리눅스 로딩 화면 없이 1~2초 이내에 X68000 Human68k가 즉시 부팅됩니다.
