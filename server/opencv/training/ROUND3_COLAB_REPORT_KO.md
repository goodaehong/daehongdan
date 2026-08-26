# 연기 현장 영상 Round 3 학습 기록

## 학습 개요

- 실행일: 2026-08-26
- 실행 환경: Google Colab, NVIDIA Tesla T4
- 기반 가중치: Round 2 `best.pt`
- 추가 원본: `연기.mp4`, `연기2.mp4`, `연기3.mp4`
- 추가 데이터: 연기 양성 변형 203장, 현장 하드 네거티브 101장
- 클래스: `0=smoke`, `1=fire`
- 학습 해상도: 416
- NCNN 내보내기 해상도: 416x256
- epoch: 18
- batch: 32
- optimizer: AdamW
- 학습 시간: 약 31초

Colab 노트북:
`https://colab.research.google.com/drive/11DvdNZOnCisvCOzm5zNxpQC6Rj3aCAY0`

## 검증 결과

- precision: 1.0000
- recall: 0.8107
- mAP50: 0.8912
- mAP50-95: 0.5979

위 수치는 이번에 추가한 현장 영상 데이터의 검증 분할 기준이다. 기존 전체 데이터셋과 독립된
일반화 성능을 뜻하지 않으므로 실제 RTSP 환경에서 오탐·미탐을 다시 확인해야 한다.

## 런타임 반영

- 모델 폴더: `smoke_yolov8n_round3_field_20260826_416x256_ncnn_model`
- 입력 크기: 416x256
- smoke confidence: 0.15
- 채널별 추론 주기: 1000 ms
- 표시 확정: 1 hit
- 마지막 검출 뒤 유지: 5000 ms

세부 학습 인자와 원본 결과는 `round3_colab_20260826/`에 보관한다.

## NCNN 프레임 단위 확인

추가 데이터의 검증 프레임에서 실제 내보낸 NCNN 모델을 confidence 0.15로 다시 실행했다.

- Round 2: 연기 양성 0/11, 하드 네거티브 오검출 0/14
- Round 3: 연기 양성 11/11, 하드 네거티브 오검출 3/14

Round 3는 이번 현장 연기를 검출하도록 개선됐지만 낮은 confidence와 1-hit 표시 설정 때문에
오탐 위험도 함께 증가했다. 현재 설정은 미탐 방지를 우선한 값이며, 실제 RTSP에서 오탐이 보이면
confidence를 먼저 0.25 이상으로 올리거나 확인 hit 수를 늘려야 한다.
