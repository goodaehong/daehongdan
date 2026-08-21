#pragma once

// 위험 상태 진입 시 경고음(WAV) 반복 재생 시작. 이미 재생 중이면 아무것도 안 함.
void SpeakerAlert_Start();

// 위험 해제 시 재생 중단. 재생 중이던 aplay도 즉시 종료시킴 (다음 곡 끝날 때까지 안 기다림).
void SpeakerAlert_Stop();

// 지금 대피 음성이 나가고 있는가. actuator_status의 voice 필드용       
// 사이렌(STM 부저)과는 별개 장치라 따로 알려야 한다
bool SpeakerAlert_IsActive();  