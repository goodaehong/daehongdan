#!/bin/bash
# 카메라 IP 바뀌면 아래 CAM_IP 한 줄만 수정하고 이 스크립트 실행하면 된다.
CAM_IP="172.20.35.109"

DIR="$(dirname "$0")"
sed "s/__CAM_IP__/$CAM_IP/g" "$DIR/mediamtx.yml" > /tmp/mediamtx.run.yml
~/mediamtx /tmp/mediamtx.run.yml
