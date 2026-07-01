사용된 라이브러리 
matplotlib>=3.7
pandas>=2.0
networkx>=3.0
pytest>=8.0

노지웅을 위한 친절한 파이썬 라이브러리 설치법!
1. 가상환경 만들기
python3 -m venv venv
2. 가상환경 활성화
source venv/bin/activate
3. 라이브러리 설치
pip install "matplotlib>=3.7" "pandas>=2.0" "networkx>=3.0" "pytest>=8.0"
4. 인터프리터 
설치가 끝나면 VS Code가 방금 만든 가상환경을 인식하도록 해야 합니다.
Cmd + Shift + P로 명령 팔레트를 엽니다.
"Python: Select Interpreter"를 검색해서 선택합니다.
목록에서 ./venv/bin/python(또는 경로에 venv가 포함된 항목)을 선택합니다.
5. 설치확인
pip list

How to use
1. log_data 폴더 안에 txt형식의 log 데이터 저장
2. __main__.py 실행
3. 분석할 txt 파일 이름 입력
4. Run