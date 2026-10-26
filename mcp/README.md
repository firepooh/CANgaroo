# cangaroo MCP 서버

cangaroo GUI를 MCP 도구로 제어한다.
Claude 같은 MCP 클라이언트가 CAN 프레임을 송수신하고 결과를 확인할 수 있다.

## 구조

```
MCP 클라이언트 ──stdio/HTTP──> cangaroo_mcp.py ──TCP JSON-RPC──> cangaroo.exe (src/remote)
                                                127.0.0.1:52790
```

- `src/remote`: cangaroo 안의 원격 제어 모듈이다. `CONFIG+=remote`로 빌드하고 `--remote`로 실행할 때만 켜진다.
- `cangaroo_mcp.py`: 공식 Python `mcp` SDK(2.x)로 만든 MCP 서버다. MCP 도구 호출을 JSON-RPC 요청으로 바꿔 전달한다.

## 준비

1. GUI를 빌드한다. `Build_Local.cmd`는 `CONFIG+=remote`를 켜고 빌드한다.
2. Python 3.10 이상으로 가상 환경을 만들고 SDK를 설치한다.

```bash
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

## 실행

먼저 cangaroo를 원격 제어 모드로 실행한다.

```bash
dist\cangaroo.exe --remote --remote-allow-tx
```

| 옵션 | 설명 |
|---|---|
| `--remote` | 127.0.0.1에서 원격 제어를 연다 |
| `--remote-port <port>` | 포트 (기본 52790, 0이면 빈 포트 자동 선택) |
| `--remote-allow-tx` | 원격 송신을 허용한다. 없으면 송신 도구는 모두 거부된다 |
| `--remote-token <token>` | 토큰을 직접 지정한다. 없으면 실행할 때마다 임의로 만든다 |
| `--remote-buffer <frames>` | 원격 조회용으로 보관할 프레임 수 (기본 100000) |
| `--workspace <file>` | 시작할 때 워크스페이스 파일을 연다 |

cangaroo는 포트와 토큰을 `%TEMP%\cangaroo-remote.json`에 기록한다.
MCP 서버는 이 파일을 읽어 접속한다.
환경 변수 `CANGAROO_HOST`, `CANGAROO_PORT`, `CANGAROO_TOKEN`으로 바꿀 수 있다.

Claude Code에는 아래처럼 stdio 서버로 등록한다. `<GUI 경로>`는 PC마다 다르므로 절대 경로로 바꿔 넣는다.

```bash
claude mcp add cangaroo -- <GUI 경로>\mcp\.venv\Scripts\python.exe <GUI 경로>\mcp\cangaroo_mcp.py
```

HTTP로 띄울 수도 있다. 이때 주소는 `http://127.0.0.1:8765/mcp`이다.

```bash
.venv\Scripts\python.exe cangaroo_mcp.py --transport http --port 8765
```

동작 확인은 `selftest.py`로 한다. 송신하지 않는 도구만 호출한다.

## 도구

| 도구 | 내용 |
|---|---|
| `status` | 서버 정보, 측정 상태, 현재 커서 |
| `list_interfaces` | 인터페이스 목록, 지원 비트레이트, 현재 설정 |
| `configure_interface` | 비트레이트, 샘플 포인트(‰), CAN FD, listen-only 설정. 측정이 멈춘 상태에서만 바꿀 수 있다 |
| `start_measurement` / `stop_measurement` | 측정 시작과 정지 |
| `bus_status` | 버스 상태, 송수신·오류 카운터 |
| `send_frame` | 프레임 1개 송신 |
| `send_periodic` / `stop_periodic` | 주기 송신 |
| `wait_for_frame` | 조건에 맞는 프레임이 올 때까지 대기 |
| `send_and_wait` | 송신 후 응답 대기. 어댑터 타임스탬프로 계산한 `latency_us`를 돌려준다 |
| `get_frames` | 커서(`since`)부터 프레임 조회. `latest=true`이면 조건에 맞는 최신 `limit`개 |
| `frame_summary` | ID별 개수, 평균·최소·최대 주기 |
| `clear_trace` / `save_trace` | 트레이스 지우기와 저장 |
| `load_dbc` / `decode_frame` / `encode_frame` | DBC 불러오기, 신호 디코딩과 인코딩 |

## CanTerminal 호환 도구

예전 CanTerminal MCP 서버(`http://127.0.0.1:5400/mcp`)에 맞춰 만든 시험 스크립트와 Claude 세션을 그대로 쓰기 위한 도구다.
STM32 로거 저장소의 `.test/hwtest/mcp_client.py`가 이 도구를 부른다.

| 도구 | 내용 |
|---|---|
| `can_status` | 채널 이름(`CAN1`, `CAN2`), 측정 중인지, 채널별 인터페이스·비트레이트·버스 상태, 받은 프레임 수 |
| `can_send` | 채널 이름으로 프레임 1개 송신. ID는 hex(`"123"`, `"0x18FF50E5"`), 데이터는 공백 없는 hex. 11비트를 넘는 ID는 확장 ID로 보낸다 |
| `can_recent` | 최신 프레임 N개(기본 50, 최대 1000)를 오래된 순으로. 채널과 ID로 거를 수 있다 |
| `can_wait_for` | 그 ID의 수신 프레임이 올 때까지 대기. 자기 송신은 맞지 않는다. 시간이 지나면 `Timed out: …` 문구를 돌려준다 |

- 결과 문구와 프레임 줄 형식은 CanTerminal과 같다: `  t=1.234567 CAN1 RX 0x123 [8] 0001020304050607 (EXT)`.
  `t`는 어댑터 타임스탬프(초)다.
- 채널 이름은 `channels.json`에서 어댑터 USB 시리얼로 정한다(기본: NO1 = `CAN1`, NO2 = `CAN2`).
  cangaroo의 `candleN` 번호는 실행할 때마다 바뀔 수 있기 때문이다. 다른 시험대는 `CANGAROO_CHANNELS`로 다른 파일을 지정한다.
  - 시리얼은 레지스트리 `HKLM\SYSTEM\CurrentControlSet\Enum\USB\VID_1D50&PID_606F\<시리얼>`의 `ParentIdPrefix`로
    CAN 인터페이스(MI_00) 경로와 이어진다. 이 경로가 cangaroo 인터페이스 `details`에 들어 있다.
  - 대응하지 못한 인터페이스는 cangaroo 이름(`candle0_ch0`)으로 쓴다.
- `can_send`도 CanTerminal처럼 ACK를 기다리지 않는다. 상대 노드가 없으면 cangaroo trace에 오류 프레임이 남는다.
- CanTerminal의 로그 파일 모드와 XCP 명령 해석은 없다. `mode`는 항상 `live`다.

실행은 `start_canterminal_compat.cmd` 하나로 한다. cangaroo를 `--remote --remote-allow-tx --remote-buffer 1000000`으로 띄우고,
MCP 서버를 5400 포트로 띄운다. 그다음 cangaroo에서 비트레이트를 맞추고 [Start]를 누른다.

## 사용할 때 주의할 점

- 타이밍은 `latency_us`, `period_*_ms` 값으로 판정한다. 도구 호출 한 번에 수백 ms가 걸리므로 호출 간격으로 판정하면 안 된다.
- 모든 프레임에는 계속 증가하는 `seq`가 붙는다. 결과의 `next`나 `cursor`를 다음 호출의 `since`로 넘기면 빠짐없이 이어서 읽을 수 있다.
- `dropped: true`는 버퍼 크기를 넘어 오래된 프레임이 지워졌다는 뜻이다.
- 실제 버스에 송신하면 연결된 ECU가 동작할 수 있다. `--remote-allow-tx`는 필요할 때만 켠다.
- 측정을 시작하면 어댑터가 버스에 ACK를 보낸다. 버스에 영향을 주지 않으려면 `listen_only=true`로 설정한다.
