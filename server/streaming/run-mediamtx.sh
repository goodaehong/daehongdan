#!/bin/bash
# 카메라 IP 바뀌면 아래 CAM_IP 한 줄만 수정하고 이 스크립트 실행하면 된다.
CAM_IP="172.20.35.182"

DIR="$(dirname "$0")"
# 여러 계정이 같은 서버를 공유하는 환경이라 /tmp 임시파일 이름에 사용자명을 넣어
# 서로 다른 계정끼리 파일을 겹쳐 쓰다 permission denied 나는 걸 피한다.
RUN_YML="/tmp/mediamtx.run.$(whoami).yml"
sed "s/__CAM_IP__/$CAM_IP/g" "$DIR/mediamtx.yml" > "$RUN_YML"
~/mediamtx "$RUN_YML"
