#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_device.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/byteorder/generic.h> // swab16

#define ADS1115_REG_CONVERSION 0x00
#define ADS1115_REG_CONFIG 	   0X01

#define ADS1115_MUX_MQ2  	 0x4000 // AIN0 vs GND (100)
#define ADS1115_MUX_MQ9  	 0x5000 // AIN1 vs GND (101)
#define ADS1115_PGA_6144     0x0000 // ±6.144V
#define ADS1115_MODE_SINGLE  0x0100
#define ADS1115_DR_128SPS    0x0080
#define ADS1115_COMP_DISABLE 0x0003
#define ADS1115_OS_START 	 0x8000

/* 디바이스별 private 데이터 구조체 */
struct ads1115_data {
	struct i2c_client *client;
	struct mutex lock; // 채널 스위칭 경합 방지
};

/* 공통 채널 읽기 함수: MUX 값만 바꿔서 재사용 */
static int ads1115_read_channel(struct ads1115_data *data, u16 mux, s16 *out) {
	struct i2c_client *client = data->client;
	u16 config;
	s32 ret;

	mutex_lock(&data->lock);

	/* 1. Config 레지스터에 채널/게인/모드 세팅 + 변환 시작(OS=1) */
	config = ADS1115_OS_START | mux | ADS1115_PGA_6144 |
			 ADS1115_MODE_SINGLE | ADS1115_DR_128SPS | ADS1115_COMP_DISABLE;

	ret = i2c_smbus_write_word_swapped(client, ADS1115_REG_CONFIG, config);
	if (ret < 0)
		goto out;

	/* 2. 변환 대기 (128 SPS 기준 약 8ms) */

