#include <linux/i2c.h> // I2C 통신을 위한 함수 및 구조체
#include <linux/module.h> // 커널 모듈 프로그래밍을 위한 매크로 (MODULE_* 등)
#include <linux/mutex.h> // 동시 접근 방지를 위한 뮤텍스(락)
#include <linux/delay.h> // msleep 등 지연 함수
#include <linux/of.h> // Device Tree (Open Firmware) 매칭을 위한 헤더

/* ADS1115 레지스터 주소 */
#define ADS1115_REG_CONVERSION 0x00 // 변환된 ADC 결과값을 읽어오는 레지스터
#define ADS1115_REG_CONFIG 0x01 // 센서 설정을 지시하는 레지스터

/* Config 레지스터에 들어갈 설정값들 - MUX 필드만 다르고 나머지는 고정 */
// OS=1(변환 시작), PGA=000(±6.144V), MODE=1(단일 변환 모드), DR=100(128SPS), COMP=0011(비활성)
#define ADS1115_CFG_BASE 0xC183 
// MQ-2 센서 채널 선택 (AIN0와 GND 비교): 비트 12~14 자리에 100(0x4) 넣음
#define ADS1115_MUX_AIN0 (0x4 << 12)
// MQ-9 센서 채널 선택 (AIN1과 GND 비교): 비트 12~14 자리에 101(0x5) 넣음
#define ADS1115_MUX_AIN1 (0x5 << 12)

/* 디바이스 드라이버 전용 데이터 구조체 (상태 유지용) */
struct ads1115_data {
    struct i2c_client *client; // 커널이 제공하는 I2C 클라이언트 객체 (통신에 사용)
    struct mutex lock; // 채널 스위칭 시 다른 프로세스가 끼어들지 못하게 막는 자물쇠
};

/* 특정 채널(mux)의 값을 읽어오는 핵심 내부 함수 */
static int ads1115_read_channel(struct ads1115_data *data, u16 mux, s16 *out) {
    int ret;
    // 1. 비트 마스킹: 베이스 설정에서 MUX 비트만 비우고, 인자로 받은 mux 값을 합침
    u16 config = mux | (ADS1115_CFG_BASE & ~(0x7 << 12));

    // 2. 통신 시작 전 자물쇠 채움 (다른 곳에서 동시에 I2C 요청하는 것 방지)
    mutex_lock(&data->lock);

    // 3. I2C 통신: ADS1115_REG_CONFIG(0x01) 레지스터에 config 값을 씀 (바이트 순서(엔디안) 변환 포함)
    ret = i2c_smbus_write_word_swapped(data->client, ADS1115_REG_CONFIG, config);
    if (ret < 0) { // 통신 실패 시 바로 종료(out)로 이동
        goto out;
    }

    // 4. ADC가 아날로그 값을 디지털로 변환할 시간 대기 (데이터 레이트 128SPS 기준 10ms면 충분)
    msleep(10); /* 128SPS 변환 시간 확보 */
    
    // 5. I2C 통신: ADS1115_REG_CONVERSION(0x00) 레지스터에서 변환 완료된 결과를 읽어옴
    ret = i2c_smbus_read_word_swapped(data->client, ADS1115_REG_CONVERSION);
    if (ret < 0) {
        goto out;
    }

    // 6. 정상적으로 읽어왔다면 인자로 받은 포인터(out)에 값을 담고, 에러 코드는 0(성공)으로 초기화
    *out = (s16)ret;
    ret = 0;

out: // 공통 정리 및 종료 지점
    // 7. 작업이 끝났으므로 자물쇠를 풂
    mutex_unlock(&data->lock); // (성공하든 실패하든) 반드시 자물쇠 해제
    return ret; // 에러코드(실패시 음수, 성공시 0) 반환 후 함수 진짜 종료
}

/* 유저 공간(sysfs)에서 'mq9_value' 파일을 읽을 때(cat) 호출되는 함수 */
static ssize_t mq9_value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    // dev 객체 안에 숨겨둔 우리 드라이버 데이터(ads1115_data)를 꺼내옴
    struct ads1115_data *data = dev_get_drvdata(dev);
    s16 raw;
    
    // MQ-9 채널(AIN1) 값 읽기 요청
    int ret = ads1115_read_channel(data, ADS1115_MUX_AIN1, &raw);
    if (ret < 0) return ret;

    // 문자열 버퍼(buf)에 값을 써서 유저에게 전달 ("%d\n" 형태)
    return sysfs_emit(buf, "%d\n", raw);
}

// 읽기 전용(RO) 속성으로 묶어줌 (sysfs에 /sys/.../mq9_value 파일 생성)
static DEVICE_ATTR_RO(mq9_value);

/* 유저 공간(sysfs)에서 'mq2_value' 파일을 읽을 때(cat) 호출되는 함수 */
static ssize_t mq2_value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct ads1115_data *data = dev_get_drvdata(dev);
    s16 raw;

    // MQ-2 채널(AIN0) 값 읽기 요청
    int ret = ads1115_read_channel(data, ADS1115_MUX_AIN0, &raw);
    if (ret < 0) return ret;

    return sysfs_emit(buf, "%d\n", raw);
}

// 읽기 전용(RO) 속성으로 묶어줌 (sysfs에 /sys/.../mq2_value 파일 생성)
static DEVICE_ATTR_RO(mq2_value);

/* 이 드라이버가 유저 공간에 노출할 속성(파일)들의 리스트 */
static struct attribute *ads1115_attrs[] = {
    &dev_attr_mq9_value.attr,
    &dev_attr_mq2_value.attr,
    NULL, // 리스트의 끝을 알림
};
ATTRIBUTE_GROUPS(ads1115);

/* 커널이 부팅/모듈 로드 시 I2C 장치(ADS1115)를 발견하면 최초로 실행되는 초기화 함수 */
static int ads1115_probe(struct i2c_client *client) {
    struct ads1115_data *data;

    // 1. 드라이버 데이터 구조체를 위한 메모리 할당 (devm_ 은 모듈 해제 시 커널이 자동 청소해줌)
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM; // 메모리 부족 에러 반환

    // 2. 초기화 세팅
    data->client = client;
    mutex_init(&data->lock); // 뮤텍스(자물쇠) 초기화

    // 3. I2C client 구조체 내부에 우리가 만든 data 구조체를 연결 (나중에 show 함수에서 꺼내 쓰기 위함)
    i2c_set_clientdata(client, data);

    // 4. 커널 로그(dmesg)에 성공적으로 연결되었음을 출력
    dev_info(&client->dev, "ADS1115 probed at addr 0x%02x\n", client->addr);
    return 0;
}

/* Device Tree(DTS)에서 이 드라이버를 찾을 때 사용하는 이름표 매칭 테이블 */
static const struct of_device_id ads1115_of_match[] = {
    { .compatible = "ti,ads1115" }, // Device Tree의 compatible 속성과 이 문자열이 같으면 probe 실행
    { }
};
MODULE_DEVICE_TABLE(of, ads1115_of_match);

/* I2C 드라이버의 전체 뼈대 구조체 */
static struct i2c_driver ads1115_driver = {
    .driver = {
        .name = "ads1115_gas", // 드라이버 이름
        .of_match_table = ads1115_of_match, // Device Tree 매칭 테이블 연결
        .dev_groups = ads1115_groups, // 유저에 노출할 sysfs 그룹 (mq2_value, mq9_value) 연결
    },
    .probe = ads1115_probe, // 장치 발견 시 실행할 함수 연결
};
// 이 매크로 하나로 module_init, module_exit 등을 자동으로 생성해줌 (보일러플레이트 코드 감소)
module_i2c_driver(ads1115_driver);

/* 커널 모듈에 대한 메타데이터 (modinfo 명령어로 확인 가능) */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuna Kim");
MODULE_DESCRIPTION("ADS1115 driver for MQ-9/MQ-2 gas sensors");