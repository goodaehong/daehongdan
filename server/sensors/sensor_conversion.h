#pragma once

// raw ADC(ADS1115) -> 물리량 환산 (순수 계산, I/O 없음)
float mq9ToGasPpm(int raw);
float mq2ToSmokePpm(int raw);
float rawToVoltage(int raw); // 불꽃센서(flame_value)용 - 전압(V) 변환