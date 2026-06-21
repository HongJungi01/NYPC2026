# 작업 지침 — submission/ (NYPC 2026 버섯밭 봇 실전 코드)

이 폴더는 사이트에 제출하는 봇 코드를 담는다. 두 파일의 역할이 **엄격히 구분**된다.

## 파일 역할

| 파일 | 역할 | 수정 규칙 |
|------|------|-----------|
| `main.cpp` | **사이트 기준 현재 최고 성적 코드 (FROZEN).** 사용자가 사이트 결과를 보고 직접 갱신한다. | ⛔ **사용자의 명시적 수정개입 지시 없이는 절대 수정 금지.** |
| `test.cpp` | **작업·실험 파일.** 앞으로의 모든 변경 요청은 여기서 한다. | ✅ 여기서 작업. 검증 통과 후 사용자가 사이트에 올려 main 갱신 여부 판단. |

> 변경 요청이 오면 **`test.cpp`** 를 수정한다. `main.cpp`는 건드리지 않는다.

## 빌드 / 검증 / 벤치 (`test.cpp`)

MSYS2 g++ 사용 시 하위 프로세스 DLL 로드를 위해 PATH에 `/c/msys64/mingw64/bin` 추가.

```sh
# 제출용 빌드 (실제 봇)
g++ -O2 -std=c++17 test.cpp -o test.exe

# 정확성 검증: 자가대국 중 매 탐색 노드에서 deriveMoves == 전체스캔 대조
g++ -O2 -std=c++17 -DTEST_MAIN -DVERIFY_MOVES test.cpp -o test_check.exe && ./test_check.exe
#   -> "[TEST_MAIN] OK ... no move-gen mismatch" 이어야 함

# 성능 벤치 (동일 트리, 시간차 = 수생성 비용)
g++ -O2 -std=c++17 -DBENCH_MAIN test.cpp -o b.exe && ./b.exe                 # 증분(deriveMoves)
g++ -O2 -std=c++17 -DBENCH_MAIN -DFORCE_FULLSCAN test.cpp -o bf.exe && ./bf.exe  # 풀스캔 기준선
```

빌드 매크로(`TEST_MAIN`/`VERIFY_MOVES`/`BENCH_MAIN`/`FORCE_FULLSCAN`)는 `#ifdef`로 격리되어
일반 제출 빌드에는 영향이 없다.

## 원칙

- **검증 후 채택**: 수생성/탐색 최적화는 차분 검증(`VERIFY_MOVES`, 또는 `tools/moveGen/` 하니스)
  **불일치 0건**을 확인한 뒤에만 채택한다. 통과 못 하면 미채택.
- **수생성 기반 도구**의 단일 출처: [../tools/TOOLS.md](../tools/TOOLS.md), [../tools/moveGen/](../tools/moveGen/).
- 현재 `test.cpp`는 negamax+AB+TT 탐색의 수생성을 4방향 적응형 증분(`deriveMoves`)으로 사용한다.
