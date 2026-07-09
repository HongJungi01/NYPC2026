# centerbug_verify — 중앙버그(center_war 재점화) 실증 실험 (07-07)

## 변형 정의 (사용자)

| 봇 | 소스 | 정의 |
|---|---|---|
| **A** | A.cpp | goldenhammer_crl_rushguard 복사본 = **중앙버그 수정**(center_war ever-built latch) 포함. rushguard 자체는 5기+ 뭉침이 없는 이 계열 미러전에선 비발동 |
| **B** | B.cpp | goldenhammer_crl 원본 스냅샷 = **중앙버그 잔존** |
| **C** | A.cpp `-DSECOND_NEAREST` | A + 두 번째 기지를 중앙 대신 '첫 기지 계획 제외 최근접' 거점에(중앙 슬롯은 3번째부터 개방) |
| **D** | B.cpp `-DSECOND_NEAREST` | B + 같은 두 번째 기지 변경 |

중앙버그 = 중앙에 지어진 건물이 중후반 파괴돼 빈 거점이 되면 상대 경유 관측만으로
center_war가 재점화 → 판정 ③⑨의 초반 올인 로직(잉여 전량 중앙 스택 + 경제 동결)이
중후반에 잘못 재적용되는 결함. latch = 누구든 한 번 지었으면 war 영구 금지.

## 매치 설계

- **A vs D**, **B vs C** 각 10판: 시드 201~205 × 양사이드 미러.
- SECOND_NEAREST 쪽이 중앙을 늦게 가므로 중앙 경합·중앙 파괴 후 재점화 상황이
  비대칭으로 발생하기 쉬움 — B(버그 잔존)의 블랙홀 발현 여부를 로그로 판독.

## 빌드

```
g++ -O2 -std=c++17 A.cpp -o A.exe
g++ -O2 -std=c++17 B.cpp -o B.exe
g++ -O2 -std=c++17 -DSECOND_NEAREST A.cpp -o C.exe
g++ -O2 -std=c++17 -DSECOND_NEAREST B.cpp -o D.exe
```

## 실행

```
python ../simulator/testing-tool.py --seed <S> -l logs/<이름>.log -a <LEFT.exe> -b <RIGHT.exe>
```

로그 파일명: `AvD_s<시드>_<A사이드>.log` (예: AvD_s201_AL = A가 LEFT).
