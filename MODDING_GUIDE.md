# RealGazebo Modding Guide

새 vehicle을 **본 게임 재빌드 없이 Pak DLC 형태로** 추가하는 방법.

이 문서는 **외부 mod 작성자**를 1순위 독자로 하는 설명서입니다. 본 게임 개발자가 한 번만 하는 작업(Release Version 빌드)은 [부록 A](#부록-a-게임-개발자용-본-게임-release-version-빌드)로 분리했습니다.

---

## 목차

1. [개요](#1-개요)
2. [Mod plugin 만들기](#2-mod-plugin-만들기)
3. [Editor에서 빠른 검증](#3-editor에서-빠른-검증)
4. [Mod pak 빌드 + 적용](#4-mod-pak-빌드--적용)
5. [UDP Packet으로 spawn 테스트](#5-udp-packet으로-spawn-테스트)
6. [트러블슈팅](#6-트러블슈팅)

부록:
- [A. 게임 개발자용: 본 게임 Release Version 빌드](#부록-a-게임-개발자용-본-게임-release-version-빌드)
- [B. PowerShell로 cook 명령 직접 실행](#부록-b-powershell로-cook-명령-직접-실행)
- [C. 참고 파일](#부록-c-참고-파일)

---

## 1. 개요

### 어떤 흐름인가

```
[게임 개발자] (한 번만)
   └─ 본 게임을 Release Version 1.0으로 cook
       └─ 결과: <Game>.exe + 본 게임 pak + Releases/1.0/Metadata/AssetRegistry.bin
       └─ Mod SDK 형태로 배포 (cooked 게임 + Releases metadata + plugin source)

[Mod 작성자] (반복)
   └─ 자기 mod plugin 만들기 (BP + DataTable)
   └─ Release Version 1.0 기반으로 DLC cook
       └─ 결과: SkyflyMod_P.pak (수 MB)
   └─ End user에게 pak 배포

[End user]
   └─ 받은 mod pak을 본 게임의 Content/Paks 폴더에 떨굼
   └─ 본 게임 실행 → mod 자동 적용
```

### Vehicle Registry 아키텍처

본 게임은 `UVehicleRegistrySubsystem`(GameInstanceSubsystem)을 통해 모든 vehicle 정의를 중앙 관리. Mod는 자기 DataTable만 본 게임에 추가하면 Registry가 AssetRegistry 스캔으로 자동 발견.

```
[코어 DT] DT_UnifiedVehicleConfig
                 ↓ ARealGazeboManager::BeginPlay
        ┌──────────────────────────────────┐
        │  UVehicleRegistrySubsystem       │
        │  - 코어 + mod row 통합 보관       │
        │  - VehicleTypeCode 충돌 정책      │
        └──────────────────────────────────┘
                 ↑ AssetRegistry 스캔으로 자동 발견
[Mod DT] DT_<modname>_Vehicles  ← mod plugin이 제공
```

여러 mod plugin이 동시에 mount되어도 다 함께 작동합니다 (`VehicleTypeCode`만 안 겹치면).

### 충돌 정책

| 충돌 | 결과 |
|---|---|
| 코어 ↔ mod 같은 `VehicleTypeCode` | 코어 우선, mod row 거부 (Error 로그) |
| mod ↔ mod 같은 `VehicleTypeCode` | 둘 다 거부 (Error 로그), 해당 code 비활성 |
| `VehicleTypeCode == 0`인 mod row | 거부 (reserved default) |

### Mod 작성자 권장사항

- `VehicleTypeCode`: 128–255 사용 (코어는 보통 0–127)
- `RowName`: `mod_<modname>_<vehicle>` 컨벤션 (강제 아니지만 디버그 가독성)
- `VehiclePawnClassSoft`: 새 row에서는 hard `VehiclePawnClass` 대신 이것 사용 (Soft ref가 mod-friendly)

---

## 2. Mod plugin 만들기

### 2-A. UE 프로젝트 셋업 (처음 한 번)

Mod 작성자에겐 자기 UE 프로젝트가 필요. 게임 개발자가 SDK로 제공한 RealGazebo plugin을 그 프로젝트에 넣어서 작업 환경 구성.

#### 1) 새 UE 프로젝트 생성

UE Editor 시작화면 → **New Project**:
- 카테고리: **Games**
- 템플릿: **Blank**
- 프로젝트 종류: **C++** (Blueprint Only도 가능하지만 plugin 모듈 빌드를 위해 C++ 권장)
- 이름: 자유 (예: `RealGazeboModKit`), 위치도 자유

엔진 버전은 **SDK가 빌드된 UE 버전과 일치** (현재 SDK는 UE 5.7).

#### 2) SDK 자료를 새 프로젝트에 배치

게임 개발자가 제공한 SDK 자료 3종을 새 프로젝트 폴더 안에 복사:

```
<새 프로젝트>/
├─ Plugins/
│   └─ RealGazebo/                              ← SDK plugin (Source + Content 통째로)
├─ Releases/
│   └─ 1.0/Windows/Metadata/
│       ├─ AssetRegistry.bin                    ← SDK metadata (DLC cook이 base로 삼음)
│       └─ DevelopmentAssetRegistry.bin
└─ <ProjectName>.uproject
```

PowerShell 예시 (경로는 본인 환경에 맞게 조정):
```powershell
$sdk  = "C:\Path\To\Received\SDK"
$mine = "C:\Path\To\Your\UnrealProjects\<YourProjectName>"

Copy-Item -Recurse "$sdk\Plugins\RealGazebo" "$mine\Plugins\"
Copy-Item -Recurse "$sdk\Releases" "$mine\"
```

#### 3) `.uproject`에 RealGazebo plugin 활성화

`<프로젝트>.uproject` 파일을 텍스트 에디터로 열고 `Plugins` 항목에 RealGazebo 추가:

```json
{
    "FileVersion": 3,
    "EngineAssociation": "5.7",
    "Modules": [ ... ],
    "Plugins": [
        { "Name": "RealGazebo", "Enabled": true }
    ]
}
```

#### 4) Visual Studio project 생성 + 빌드

1. `.uproject` 우클릭 → **"Generate Visual Studio project files"**
   - Windows 11에서 메뉴 안 보이면 우클릭 → **"Show more options"** → 해당 항목
2. 생성된 `<프로젝트>.sln` 더블클릭 → VS에서 열기
3. Solution Configuration: **Development Editor**, Platform: **Win64**
4. Solution Explorer에서 **Games** 폴더 펼치기 → 프로젝트 우클릭 → **"Set as Startup Project"**
5. **Build** (Ctrl+Shift+B) → 첫 빌드 시 셰이더 컴파일로 10-30분 소요

#### 5) Editor 실행 + 검증

빌드 성공하면 `.uproject` 더블클릭으로 Editor 실행. Content Browser에서:
- 좌측 하단 톱니바퀴 → **"Show Plugin Content"** ✓
- 좌측 트리에 **`RealGazebo Content`** 폴더 보이면 OK

여기까지가 1회성 셋업. 이제 mod plugin 만들 준비 완료.

---

### 2-B. Mod plugin 생성 (Editor)

> ⚠️ **본 가이드의 mod 이름은 전부 예시.** 아래 본문에서 `SkyflyMod`, `BP_SkyflyDrone`, `DT_SkyflyMod_Vehicles` 같은 이름은 모두 **자신이 정한 mod plugin 이름으로 일관되게 치환**해서 읽어주세요. 본 게임 코드는 plugin 이름을 hardcode 하지 않고 런타임에 자동 인식합니다.
>
> 단 한 번 정한 이름은 가이드 전체에서 **동일하게** 유지해야 합니다 (plugin 폴더명 = `.uplugin` 파일명 = DLC cook 옵션의 DLC 이름 = pak 파일명 접두사). 한 곳이라도 달라지면 cook 실패하거나 본 게임이 인식 못 함.

1. UE Editor → **Edit → Plugins**
2. 우측 상단 **"+ Add"** 클릭
3. 좌측 카테고리에서 **"Content Only"** 선택
4. 우측 패널에서:
   - **Plugin Name**: 본인이 정할 이름 (예: `SkyflyMod`, `MyAwesomeMod`, 자유). **이 이름을 메모해 두세요** — 이후 절차에 계속 등장합니다.
   - **Author**, **Description**: 자유
5. **Create Plugin** 클릭 → 재시작 prompt 뜨면 재시작

결과 (이름을 `SkyflyMod`로 정했을 경우 예시):
```
Plugins/SkyflyMod/
  ├─ SkyflyMod.uplugin
  ├─ Content/
  └─ Resources/Icon128.png
```

### 2-C. Pawn BP 만들기

Mod의 vehicle BP는 **코어 base BP의 child**로 만드는 게 정석:

1. Content Browser → "Show Plugin Content" 체크 → 좌측 트리에서 **`RealGazebo Content/Blueprints/Vehicles/RealGazebo/`** 로 이동
2. **`BP_VehicleBasePawn_RealGazebo`** 우클릭 → **"Create Child Blueprint Class"**
   - 이 BP가 카메라 컨트롤러 + 1인칭/3인칭 카메라 + Bridge 기능을 다 포함한 **정석 베이스**. 다른 `BP_VehicleBasePawn` (Bridge), `BP_VehicleBasePawn_UI` (카메라만) 도 있지만 mod 작성자는 `_RealGazebo` 베이스만 쓰면 됨.
3. 저장 위치 dialog에서 자기 mod plugin Content 폴더로 이동 (예: `SkyflyMod Content`)
4. 이름 정하기 (예: `BP_SkyflyDrone`, 자유) → Save
5. 더블클릭으로 열기 → 메시 + 컴포넌트 셋업:
   - **메시 추가**: VehicleMesh 슬롯에 본인 차량 SkeletalMesh/StaticMesh 박기
     - 검증 목적이면 `Engine Content/BasicShapes/Cube`도 OK
     - "Show Engine Content" 체크 필요
   - **모터/프롭** (vehicle에 회전 부품이 있는 경우):
     1. 프롭 메시를 추가하고
     2. 각 프롭 아래에 **`Rotating Movement Component`** (UE 내장) 추가
     3. Class Defaults → Details → `Bridge|Components` → **`Rotating Components`** 배열에 위에서 만든 모든 `RotatingMovementComponent` 들을 슬롯에 채워 넣기 ★ (자동 등록 안 됨, 수동 필요)
     4. DataTable의 `MotorCount` 가 이 배열 길이와 일치해야 함 (불일치 시 motor 패킷 reject)
   - **카메라 컴포넌트 (베이스 `_RealGazebo` 부모 사용 시 자동 상속)**:
     - 정상 상속됐다면 컴포넌트 트리에 회색(상속 표시)으로 다음 3개가 보임:
       - `RealGazebo Camera Controller`  ← 카메라 토글 진입점 (`URealGazeboCameraControllerComponent`)
       - `RealGazebo First Person Camera`  ← 1인칭 (`URealGazeboFirstPersonCameraComponent`)
       - `RealGazebo Third Person Camera`  ← 3인칭 SpringArm 내장 (`URealGazeboThirdPersonCameraComponent`)
     - **셋 중 하나라도 안 보이면 부모 클래스 확인** (Class Defaults → Parent Class). `_RealGazebo` 가 아니면 컴포넌트가 빠짐.
     - 만약 다른 베이스 (e.g. `BP_VehicleBasePawn` Bridge) 를 부모로 두었다면 Add Component → `RealGazebo Camera Controller` + `First Person Camera` + `Third Person Camera` 셋을 **모두 직접 추가** (컨트롤러 빠지면 카메라 전환 자체가 동작 안 함)
6. Compile + Save

⚠️ **Duplicate 사용 금지** — 코어 BP를 우클릭 → Duplicate으로 복사하면 `Template Mismatch` ensure 발생. 반드시 **Create Child Blueprint Class** 사용.

### 2-D. DataTable 만들기

1. Content Browser → `SkyflyMod Content` 안에서 우클릭
2. **기타 → 데이터 테이블**
3. Row Structure 선택 dialog → 검색 `RealGazeboVehicle` → **`RealGazeboVehicleConfigRow`** 선택 → OK
4. 이름 `DT_SkyflyMod_Vehicles` → Save
5. 더블클릭으로 DataTable Editor 열기 → 상단 **"+ Add"** 누르고 row 추가:
   - **Row Name**: `skyfly_drone_x`
   - **Vehicle Name**: `Skyfly_X`
   - **Vehicle Type Code**: `128` (코어와 안 겹치는 값)
   - **Motor Count**: `4` (실제 BP의 모터 개수에 맞춰야 함)
   - **Servo Count**: `0`
   - **Vehicle Pawn Class (Legacy)**: 비워두기
   - **Vehicle Pawn Class** (Soft): `BP_SkyflyDrone` 선택 ★
   - **Show In UI**: ✓
6. Save

---

## 3. Editor에서 빠른 검증

mod 자산이 잘 만들어졌는지 PIE에서 5분 안에 sanity check:

1. UE Editor에서 `RealGazebo` 맵 (또는 `ARealGazeboManager` actor가 배치된 맵) 열기
2. **Window → Output Log** 열기
3. **▶ Play** (PIE 시작)
4. Output Log에서 다음 순서대로 확인:
   ```
   LogRealGazebo: VehicleRegistry: Initializing
   LogRealGazebo: VehicleRegistry: Core source 'DT_UnifiedVehicleConfig' registered (N total type codes)
   LogRealGazebo: VehicleRegistry: merged 'DT_SkyflyMod_Vehicles' (mod) - added=1 rejected=0
   LogRealGazebo: VehicleRegistry: mod scan complete - 1 mod DataTable(s) found
   LogRealGazebo: BridgeSubsystem: pushed N+1 vehicle configs from registry
   ```
5. PowerShell로 UDP packet 보내서 spawn 화면 확인 → [§5](#5-udp-packet으로-spawn-테스트)

PIE에서 작동하면 mod 자산 설정은 OK. 단 **PIE는 모든 자산을 in-memory로 접근하므로, packaging cook에서도 작동한다는 보장은 안 됨** → §4로 진행.

---

## 4. Mod pak 빌드 + 적용

이게 진짜 distribution 모델. mod 자산만 별도 pak으로 만들어서 cooked 본 게임에 떨구면 작동.

> ⚠️ **본 게임 이름도 예시.** 본 가이드는 본 게임 프로젝트명이 `RealGazeboTest` 라고 가정. 본인이 받은 SDK의 본 게임 이름이 다르면 (예: `C_Track`, `MyGame` 등) 본문의 `RealGazeboTest` 를 그 이름으로 일관되게 치환해서 읽어주세요. 본 게임 이름은 SDK 제공자(게임 개발자)가 정합니다.

### 전제조건

- ✅ Mod plugin 완성 (§2)
- ✅ PIE 검증 통과 (§3)
- ✅ **`Releases/1.0/Windows/Metadata/AssetRegistry.bin` 존재** — 게임 개발자가 SDK로 제공
   - 없으면 → [부록 A](#부록-a-게임-개발자용-본-게임-release-version-빌드)에서 먼저 release version 빌드
- ✅ Cooked 본 게임 (`<출력>/Windows/RealGazeboTest.exe` + 본 게임 pak) — 같이 SDK로 제공

### 4-A. Mod pak cook (Project Launcher)

> 실제 작동한 설정 스크린샷:
> - 상단부: [`Docs/ProjectLauncher_Profile2_DLC_Top.png`](Docs/ProjectLauncher_Profile2_DLC_Top.png)
> - 하단부: [`Docs/ProjectLauncher_Profile2_DLC_Advanced.png`](Docs/ProjectLauncher_Profile2_DLC_Advanced.png)

1. **Window → Project Launcher** (한국어 UI: **창 → 프로젝트 런처**)
2. 상단 **"Custom Launch Profiles"** 옆 **`+`** → 이름 `SkyflyMod DLC` → 생성
3. profile 우측 **연필 아이콘** 클릭 (편집 모드)

#### 빌드 섹션
- 빌드할까요? = **"빌드하지 않음"** (release version에 이미 본 게임 빌드 포함됨)
- 빌드 환경설정: **Shipping** (distribution용. 디버깅이 필요한 경우만 Development)

#### 쿠킹 섹션
- 콘텐츠 쿠킹: **"By the book"**
- 쿠킹된 플랫폼: **Windows** ✓
- 쿠킹된 컬처: default
- 쿠킹된 맵: **모두 표시** (NewMap 체크 해제 — DLC는 map 안 cook)
- **릴리스 / DLC / 패치 세팅** 펼치기:
  - ✗ 게임의 배포용 릴리스 버전을 만듭니다
  - **관련된 릴리스 버전**: `1.0` ★ (release version metadata가 가리키는 버전)
  - ✗ 패치 생성
  - ✓ **DLC 빌드** ★
  - 빌드할 DLC 이름: **SkyflyMod** (plugin 폴더 이름과 정확히 일치)
  - ✓ **엔진 콘텐츠 포함** ★ (없으면 engine content reference error로 abort됨)
- **고급 세팅** 펼치기:
  - ✓ 콘텐츠 압축
  - ✓ 버전 없이 패키지 저장
  - ✓ **모든 콘텐츠를 하나의 파일에 저장 (UnrealPak)** ★ 필수
  - 쿠커 빌드 환경설정: **Shipping**

#### 패키지 섹션
- 빌드 패키징: **로컬에 패키징 & 저장**
- 로컬 디렉터리 경로: `<Project>/Plugins/SkyflyMod/Saved/StagedBuilds/` (Saved부터 없을수도 있음)
- ✓ **최적화된 로딩(I/O 스토어)을 위해 컨테이너 파일을 사용합니다** ★ 필수

#### 아카이브 / 디플로이
- 아카이브 ✗, 디플로이 안 함

→ profile 저장 → ▶ **Launch this Profile**

소요: **10~20분**.

#### 완료 검증

결과 pak 위치:
```
Plugins/SkyflyMod/Saved/StagedBuilds/Windows/RealGazeboTest/Plugins/SkyflyMod/Content/Paks/Windows/
  └─ SkyflyMod*.pak  (수 MB)
  └─ SkyflyMod*.ucas
  └─ SkyflyMod*.utoc
```

⚠️ **pak 크기 검사**: 수 MB여야 정상. **100MB+면 fail** (release version 인식 실패 → 본 게임 자산까지 중복 cook됨). [§6 트러블슈팅](#6-트러블슈팅) 참조.

### 4-B. Pak을 cooked 본 게임에 떨굼

파일 탐색기로 3개 파일 (`SkyflyMod*.pak`, `.ucas`, `.utoc`)을 cooked 본 게임의 pak 폴더에 복사:

```
<cooked-game>/Windows/RealGazeboTest/Content/Paks/
  ├─ RealGazeboTest-Windows.pak     ← 본 게임 pak
  ├─ global.ucas / global.utoc      ← 본 게임 IoStore
  └─ SkyflyMod*.pak / .ucas / .utoc ← ★ 여기에 추가
```

또는 PowerShell:
```powershell
$dlcDir  = "<Project>/Plugins/SkyflyMod/Saved/StagedBuilds/Windows/RealGazeboTest/Plugins/SkyflyMod/Content/Paks/Windows"
$gameDir = "<cooked-game>/Windows/RealGazeboTest/Content/Paks"
Copy-Item "$dlcDir/SkyflyMod*" $gameDir
ls $gameDir
```

### 4-C. 실행 + 검증

1. `<cooked-game>/Windows/RealGazeboTest.exe` 실행
2. PowerShell UDP packet 전송 → 화면에 mod vehicle spawn 확인 ([§5](#5-udp-packet으로-spawn-테스트))

**Spawn까지 작동하면 검증 완료** — "본 게임 재빌드 없이 mod pak 추가만으로 mod 적용" 시나리오 입증.

> **세부 log로 진단하려면** (옵션):
> Shipping 빌드는 `LogRealGazebo` 같은 카테고리가 strip되어 안 보임. 문제 진단 필요 시 Profile에서 **빌드 환경설정 = Development**로 잠시 변경해서 한 번 cook → 다음 4줄로 단계별 확인:
> ```
> LogPakFile: Mounting Pak file '...SkyflyMod*.pak'
> LogPluginManager: Mounting Project plugin SkyflyMod
> LogRealGazebo: VehicleRegistry: merged 'DT_SkyflyMod_Vehicles' (mod) - added=1 rejected=0
> LogRealGazebo: VehicleRegistry: mod scan complete - 1 mod DataTable(s) found
> ```
> Log 파일 위치: `<cooked-game>/Windows/RealGazeboTest/Saved/Logs/RealGazeboTest.log`

### 4-D. End user에게 배포

배포할 파일:
- `SkyflyMod*.pak`
- `SkyflyMod*.ucas`
- `SkyflyMod*.utoc`

End user는 받은 3개 파일을 자기 본 게임 `Content/Paks/` 폴더에 떨구기만 하면 됨. 본 게임 재실행하면 자동 인식.

---

## 5. UDP Packet으로 spawn 테스트

PIE 또는 cooked exe 실행 중일 때 가짜 packet 전송:

### Pose packet (spawn 트리거)

```powershell
$bytes = New-Object byte[] 31
$bytes[0] = 0      # VehicleNum
$bytes[1] = 128    # VehicleType (mod에 등록한 type code)
$bytes[2] = 1      # MessageID = pose

# Position (Gazebo meters → Unreal cm 변환은 내부에서 처리)
[System.BitConverter]::GetBytes([float]0.0).CopyTo($bytes, 3)   # X
[System.BitConverter]::GetBytes([float]0.0).CopyTo($bytes, 7)   # Y
[System.BitConverter]::GetBytes([float]5.0).CopyTo($bytes, 11)  # Z = 5m

# Quaternion identity
[System.BitConverter]::GetBytes([float]0.0).CopyTo($bytes, 15)
[System.BitConverter]::GetBytes([float]0.0).CopyTo($bytes, 19)
[System.BitConverter]::GetBytes([float]0.0).CopyTo($bytes, 23)
[System.BitConverter]::GetBytes([float]1.0).CopyTo($bytes, 27)

$client = New-Object System.Net.Sockets.UdpClient
$client.Send($bytes, $bytes.Length, "127.0.0.1", 5005)
$client.Close()
```

### Motor speed packet (모터 회전)

```powershell
$motor = New-Object byte[] 19  # 3 header + 4 motors * 4 bytes
$motor[0] = 0      # VehicleNum
$motor[1] = 128    # VehicleType
$motor[2] = 2      # MessageID = motor speed

# 모터 4개의 속도 (deg/sec)
[System.BitConverter]::GetBytes([float]30.0).CopyTo($motor, 3)
[System.BitConverter]::GetBytes([float]90.0).CopyTo($motor, 7)
[System.BitConverter]::GetBytes([float]180.0).CopyTo($motor, 11)
[System.BitConverter]::GetBytes([float]360.0).CopyTo($motor, 15)

$client = New-Object System.Net.Sockets.UdpClient
$client.Send($motor, $motor.Length, "127.0.0.1", 5005)
$client.Close()
```

⚠️ **Motor packet size는 DataTable의 `MotorCount`와 일치해야 함**:
- 사이즈 = `3 + MotorCount * 4`
- 4 모터 = 19 bytes
- 6 모터 = 27 bytes
- 미스매치 시 packet drop됨

### 성공 시 log

```
LogRealGazeboBridge: Found vehicle class for type 128: BP_SkyflyDrone_C
LogRealGazeboBridge: Spawned vehicle: 128_0
LogRealGazeboUI: Vehicle 128_0 added to UI list
```

---

## 6. 트러블슈팅

### "mod scan complete - 0 mod DataTable(s) found"
- AssetRegistry가 mod의 DataTable을 못 찾음
- 원인 1: mod plugin이 disable됨 → `.uproject` Plugins에 entry 추가 또는 `.uplugin`에 `"EnabledByDefault": true`
- 원인 2: Pak이 mount 안 됨 → log에서 `LogPakFile: Mounting Pak file '...SkyflyMod*.pak'` 줄 있나 확인

### "No configuration found for vehicle type N"
- Registry에 type code N이 등록 안 됨
- 위 "mod scan complete - 0" 이슈와 동일 원인 가능

### "tried to override core type code N - rejected"
- mod가 코어와 같은 `VehicleTypeCode`를 사용 → DT에서 다른 code로 변경 (128–255 권장)

### "mod-mod conflict on type code N - both rejected"
- 두 mod가 같은 code 사용 → mod들의 code 컨벤션 합의 필요

### "VehicleTypeCode=0 (reserved default) - rejected"
- mod row에서 VehicleTypeCode를 비웠거나 0으로 설정 → 1 이상으로 변경

### Cooked mod pak이 100MB+
- Release version 인식 실패. release version 1.0이 정확한 경로(`Releases/1.0/Windows/Metadata/`)에 있는지 확인. DLC cook 옵션에서 `관련된 릴리스 버전` = `1.0`이 맞게 들어갔는지

### Cooked log에 LogRealGazebo가 거의 없음
- Shipping configuration의 정상 동작 — Log/Display 레벨이 strip됨
- 검증/디버깅 시점에만 잠깐 Development로 재빌드 (Profile에서 빌드 환경설정 변경)
- 평소 distribution은 Shipping 그대로 유지

### Child BP 만들 때 Template Mismatch ensure
- 코어 plugin의 알려진 cosmetic ensure (UE 5.7 nested subobject duplication). 동작 영향 0. F5로 진행하거나 Development Editor 사용. GitHub Issue tracker에 P3로 등록됨

### Cook은 성공인데 pak 파일이 안 만들어짐 (`Package command time: 0.00 s`)
- log에 `********** PACKAGE COMMAND STARTED **********` 다음 곧바로 `COMPLETED`가 0초 만에 뜨고, staged 폴더에 raw `.uasset` 파일들만 있는 경우
- **원인**: Profile의 두 옵션이 OFF
- **Fix**: Profile 편집 → 다음 둘 다 ✓
  - 쿠킹 → 고급 세팅 → **모든 콘텐츠를 하나의 파일에 저장 (UnrealPak)**
  - 패키지 섹션 → **최적화된 로딩(I/O 스토어)을 위해 컨테이너 파일을 사용합니다**

### DLC cook이 `Uncooked Engine or Game content ... is being referenced by DLC!` 에러로 실패
- 4가지 패턴: `Axis_Guide.uasset`, `DefaultParticle.uasset`, `ChaosVD/Box.uasset`, `DatasmithContent/AliasReference.uasset` 등
- **원인**: mod BP가 코어 base BP의 dependency를 통해 engine content를 reference. DLC cook은 strict mode (`-errorOnEngineContentUse`)라서 abort
- **Fix**: §4-A의 쿠킹 → 릴리스/DLC/패치 세팅 → ✓ **엔진 콘텐츠 포함**
- 코어 plugin 자산 누락 시 (`BP_VehicleBasePawn_UI` 등): 게임 개발자가 release 빌드할 때 Project Settings → 패키징 → "Additional Asset Directories to Cook"에 `/RealGazebo` 추가해서 다시 release 만들어야 함 (부록 A 참조)

### Mod pak이 정상 만들어졌는데 cooked exe가 인식 안 함 (`LogPakFile: Mounting Pak` 로그 없음)
- **원인 1**: Pak 파일 위치가 잘못됨 — `<cooked-game>/Windows/RealGazeboTest/Content/Paks/` 안의 본 게임 pak과 같은 폴더여야 함
- **원인 2**: 본 게임 cook이 자산을 pak으로 안 묶고 raw uasset으로 stage된 상태 → 게임 개발자가 release 빌드 시 UnrealPak + IoStore 둘 다 켜야 함 (부록 A 참조)

### `Failed to copy ... deleting, waiting 10s and retrying` 무한 반복
- **원인**: 출력 폴더에 이전 cooked exe가 lock된 상태 (다른 프로세스에서 실행 중이거나 Defender 스캔)
- **Fix**:
  1. UAT 강제 종료 (Ctrl+C 또는 창 닫기)
  2. Task Manager에서 `RealGazeboTest.exe`, `UnrealEditor-Cmd.exe` 등 종료
  3. 출력 폴더 삭제 또는 **새 이름** archive 출력 사용 (lock 회피)
  4. Windows Defender 실시간 보호에서 프로젝트 폴더 제외 (선택)
  5. 재시도

### Mod pak 이름이 `<DLCName><ProjectName>-<Platform>.pak` 형태로 만들어짐
- UE 5.7 BuildCookRun DLC mode의 기본 naming. 정상. `_P` suffix가 없어 보이지만 `.ucas`/`.utoc`가 같이 있으면 UE가 자동 mount

---

## 부록 A: 게임 개발자용 — 본 게임 Release Version 빌드

**이 섹션은 mod 작성자가 보는 게 아닙니다.** 본 게임 개발자가 새 release를 만들 때 한 번만 하는 작업.

> 실제 작동한 설정 스크린샷:
> - 상단부: [`Docs/ProjectLauncher_Profile1_Release_Top.png`](Docs/ProjectLauncher_Profile1_Release_Top.png)
> - 하단부: [`Docs/ProjectLauncher_Profile1_Release_Advanced.png`](Docs/ProjectLauncher_Profile1_Release_Advanced.png)

### 사전 설정 (한 번)

Project Settings → 패키징 → 검색 `Additional Asset` → **"Additional Asset Directories to Cook"** 어레이에 `/RealGazebo` 추가. 이게 없으면 코어 plugin의 일부 자산이 cook에 누락되어, mod 작성자의 DLC cook이 "uncooked content referenced" 에러로 abort함.

### Project Launcher Profile

1. **Window → Project Launcher**
2. **Custom Launch Profiles** → `+` → 이름 `Release 1.0` → 편집

#### 빌드 섹션
- 빌드할까요? = **"자동으로 탐지"** (또는 빌드)
- 빌드 환경설정: **Shipping** (distribution용. SDK 배포 전 검증할 때만 Development)

#### 쿠킹 섹션
- 콘텐츠 쿠킹: **"By the book"**
- 쿠킹된 플랫폼: **Windows** ✓
- 쿠킹된 컬처: default
- 쿠킹된 맵: **NewMap** ✓ (또는 시작 맵)
- **릴리스 / DLC / 패치 세팅**:
  - ✓ **게임의 배포용 릴리스 버전을 만듭니다**
  - 새로 만들 릴리스 이름: 비워두기
  - **관련된 릴리스 버전**: `1.0` (적절한 버전)
  - ✗ 패치 생성
  - ✗ DLC 빌드
  - ✗ 엔진 콘텐츠 포함
- **고급 세팅**:
  - ✓ 콘텐츠 압축
  - ✓ 버전 없이 패키지 저장
  - ✓ **모든 콘텐츠를 하나의 파일에 저장 (UnrealPak)** ★ 필수
  - 쿠커 빌드 환경설정: **Shipping**

#### 패키지 섹션
- 빌드 패키징: **로컬에 패키징 & 저장**
- 로컬 디렉터리: `<Project>/pack_release/`
- ✓ **최적화된 로딩(I/O 스토어)을 위해 컨테이너 파일을 사용합니다** ★ 필수

#### 아카이브 섹션
- ✓ 아카이브에 보관할까요?
- 아카이브 디렉터리: `<Project>/pack_release_v2/` (또는 다른 이름 — 기존 폴더와 lock 충돌 회피)

#### 디플로이
- 디플로이 않음

→ ▶ **Launch this Profile**

소요: **20~40분**.

### 완료 검증

- `<Project>/Releases/1.0/Windows/Metadata/` 폴더에 `AssetRegistry.bin` 또는 `DevelopmentAssetRegistry.bin` 존재
- 아카이브 디렉터리 (`pack_release_v2/Windows/`)에:
  - `RealGazeboTest.exe`
  - `RealGazeboTest/Content/Paks/` 폴더 + `*.pak`, `*.ucas`, `*.utoc` 자동 생성

### SDK로 배포할 것

Mod 작성자에게 줘야 할 자료:
1. **Cooked 본 게임** (`pack_release_v2/Windows/` 전체)
2. **Release version metadata** (`Releases/1.0/Windows/Metadata/`)
3. **Plugin 소스** (`Plugins/RealGazebo/` 통째로, mod 작성자가 자기 UE 환경에서 빌드)

### 새 release 만들 때

본 게임 코드/자산이 바뀔 때마다 release version을 1.1, 1.2 식으로 올리면서 같은 절차 반복. 단 mod 작성자는 자기가 따라가는 release version에 `관련된 릴리스 버전` 옵션을 맞춰서 DLC cook해야 함.

---

## 부록 B: PowerShell로 cook 명령 직접 실행

Project Launcher UI 대신 PowerShell 명령:

```powershell
# 게임 개발자: Release version 1.0 빌드
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" `
  BuildCookRun `
  -project="<Project>/RealGazeboTest.uproject" `
  -platform=Win64 -clientconfig=Shipping `
  -build -cook -stage -package -pak `
  -createreleaseversion=1.0 `
  -archivedirectory="<Project>/pack_release_v2"

# Mod 작성자: SkyflyMod DLC 빌드
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" `
  BuildCookRun `
  -project="<Project>/RealGazeboTest.uproject" `
  -platform=Win64 -clientconfig=Shipping `
  -cook -stage -pak `
  -DLCName=SkyflyMod -basedonreleaseversion=1.0
```

---

## 부록 C: 참고 파일

- **GitHub Issues** — 알려진 이슈는 plugin repo의 Issues tracker에서 검색 (`label:P3`, `label:ue5.7` 등)
- `Source/RealGazebo/Public/Core/VehicleRegistrySubsystem.h` — Registry 공개 API
- `Source/RealGazebo/Public/Data/RealGazeboVehicleData.h` — Row struct 정의
- `Source/RealGazebo/Private/Tests/VehicleRegistrySubsystemTests.cpp` — 단위 테스트 (Automation Framework)
- `Docs/` — Project Launcher 설정 스크린샷
