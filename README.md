# Discomfort Index(불쾌지수) System


![Image](https://github.com/user-attachments/assets/75b03121-60c8-4688-a740-2b048f7f456f)

본 프로젝트는 RTC 기반 시계 기능에 온·습도 데이터를 결합, 불쾌지수를 계산해 OLED/LED UI로 시각화하는 **임베디드 환경 모니터링 시스템** 입니다. 
<br> 단순 라이브러리 사용이 아닌, 커널 공간에서 GPIO 인터럽트를 처리하고 디바이스 드라이버를 구현하여 User Space와 안전하게 데이터를 교환하는 구조를 설계했습니다.

## 프로젝트 개요
단순히 라이브러리를 사용하는 수준을 넘어, 커널 공간(Kernel Space)에서 직접 GPIO 인터럽트를 핸들링하고 디바이스 드라이버를 작성하여 유저 공간(User Space) 애플리케이션과 데이터를 주고받는 전체 프로세스를 구현합니다.

## 프로젝트 목표

###  커널 드라이버 기반 하드웨어 제어
- GPIO 및 센서 프로토콜(Single-bus, 3-Wire) 직접 제어
- 로터리 인코더 인터럽트(IRQ) 처리 및 디바운싱
- Character Device Driver를 통한 커널 ↔ 유저 공간 통신

###  실시간 환경 데이터 처리
- DHT11 온도/습도 수집 + 캐싱 관리
- DS1302 RTC 기반 실시간 시계 구현 및 시간 수정
- 불쾌지수(DI) 단계 판별 (Good / Mild / Bad / Hot)

###  시각화 & UI
- I2C OLED 기반 실시간 UI / 아이콘 출력
- 8-채널 LED Bar로 DI 단계 시각화
- 로터리 인코더 입력으로 페이지 전환 & 설정 모드 진입
---

## 하드웨어 구성

| 구분 | 부품명 | 역할 | 인터페이스 |
| :--- | :--- | :--- | :--- |
| **Main Board** | **Raspberry Pi 4** | 시스템 제어 및 연산 | Linux Kernel (64-bit) |
| **Sensors** | **DHT11** | 온습도 데이터 수집 | Single-bus Protocol |
| **Sensors** | **DS1302** | 실시간 시간(RTC) 제공 | 3-Wire Interface |
| **Sensors** | **Rotary Encoder** | 페이지 전환 및 시간 설정 | Interrupt (IRQ) |
| **Display** | **OLED (SSD1306)** | 정보 및 아이콘 출력 | I2C Protocol |
| **Output** | **8-Channel LED** | 불쾌지수 레벨 시각화 | GPIO Array |

## 회로도
<img width="600" alt="image" src="https://github.com/user-attachments/assets/7c677677-9d00-409f-9a0f-dedf87f5ebdc" />


## 시스템 동작 흐름
<img width="600" alt="image" src="https://github.com/user-attachments/assets/7f96fdf5-7a80-491b-b8cc-0cca061a252b" />

- 커널 드라이버가 DHT11 / DS1302를 주기적으로 갱신 (≈2s)
- 로터리 인코더 조작 시 IRQ 발생 → UI 전환
- 유저 앱이 센서값을 읽고 **불쾌지수(DI) 계산**

  - ⏰ 시계 화면
  - 🌡 온습도 페이지
  - 😵 불쾌지수 + 아이콘 표시

- LED Bar
  - DI 값에 따라 점등 개수 자동 조절

### EDIT(시간 수정) 모드
- 인코더 버튼 **롱프레스 → 설정 진입**
- 회전으로 시/분/초 변경
- RTC에 저장

## 기술 구현
<img width="600" alt="image" src="https://github.com/user-attachments/assets/0a2f0312-0496-4193-bdd9-b6172d8d00b1" />

### 1) User–Kernel 분리 구조 설계
- 커널 영역에서 **센서·입력·시간·LED 제어** 수행
- User Space에서는 **OLED UI 렌더링** 전담
- `/dev/clock_drv`를 통한 안전한 상태 동기화
- UI 변경 시 커널 수정 없이 유저 앱만 수정 가능
   
### 2) GPIO 인터럽트 기반 Rotary Encoder 입력 처리
- GPIO IRQ를 이용한 로터리 엔코더 회전(S1) 감지
- 방향(S2) 신호를 이용한 CW / CCW 판별
- 버튼 입력을 Short / Long Press로 구분 처리
- jiffies 기반 디바운싱으로 채터링 문제 해결

### 3) FSM 기반 Clock UI 상태 제어
- Clock UI를 **RUN / EDIT 모드**로 명확히 분리
- EDIT 모드에서만 시간 변경 가능하도록 제한
- Clock 페이지(Page 0)에서만 설정 진입 허용
- 예외 입력 상황에서도 상태 불일치가 발생하지 않도록 FSM 설계

### 4) DHT11 센서 직접 제어 및 캐싱 전략
- GPIO 타이밍 기반 DHT11 프로토콜 직접 구현
- 인터럽트 비활성화 구간을 활용해 타이밍 정확도 확보
- 2초 단위 캐싱(DHT_CACHE_MS) 적용
  - 센서 불안정성 감소
  - 불필요한 반복 측정 방지
    
## 파일 구조
- `driver.c`: 리눅스 커널 모듈 소스 코드
- `application.c`: 유저 애플리케이션 (OLED 및 메인 로직)
- `Makefile`: 커널 빌드 환경(`ARCH=arm64`) 설정

