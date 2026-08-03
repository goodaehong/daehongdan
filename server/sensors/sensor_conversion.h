#pragma once

// DHT22 온습도 sysfs 읽기
bool readDHT22(float& out_temp, float& out_hum);

// ADS1115 raw ADC 값 읽기 (MQ-9, MQ-2)
bool readADS1115(int& raw_mq9, int& raw_mq2);

// raw ADC -> 물리량(ppm) 환산
float mq9ToGasPpm(int raw);
float mq2ToSmokePpm(int raw);