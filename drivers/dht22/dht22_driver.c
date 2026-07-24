#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/of.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <linux/kernel.h>   // abs() 매크로 여기 있음

#define DHT22_BITS_PER_READ      40
#define DHT22_DECODE_SPAN        (2 * DHT22_BITS_PER_READ + 1)  // 81 - 디코딩에 필요한 최소 연속 엣지 수
#define DHT22_EDGES_PER_READ     85   // 배열 상한(오버플로 방지용). 완료 트리거로는 쓰이지 않음 - 실제 완료 판정은 DHT22_DECODE_SPAN 기준

#define DHT22_START_LOW_US_MIN 1000 // start pulse: 최소 1ms
#define DHT22_START_LOW_US_MAX 2000 // 최대 2ms
#define DHT22_WAIT_TIMEOUT_JIFFIES HZ // 최대 1초 대기

#define DHT22_EDGES_PREAMBLE 2 // start 응답용 low+high 2개 엣지
#define DHT22_BIT_THRESHOLD_NS 49000 // 이 값보다 HIGH 폭이 길면 1, 짧으면 0

#define DHT22_CACHE_VALID_NS 2000000000LL // 2초 (DHT22 최소 측정 간격 스펙)

struct dht22_edge {
    s64 ts; // ktime_get_bottime_ns() 값
    int value; // 그 순간의 GPIO 레벨 (0 또는 1)
};

struct dht22_data {
    struct device *dev;
    struct gpio_desc *gpiod;
    int irq;

    struct completion completion; // ISR이 다 채우면 이걸로 깨움
    struct mutex lock; // sysfs 동시 read 방지

    struct dht22_edge edges[DHT22_EDGES_PER_READ];
    int num_edges; // -1 = "측정 중 아님", ISR이 이 값 보고 배열 채움

    s64 last_read_ns; // 캐시 판단용 (2초 이내면 재측정 안 함)
    int temperature; // 0.1도 단위로 저장
    int humidity; // 0.1% 단위로 저장
};

// ISR
static irqreturn_t dht22_irq_handler(int irq, void *dev_id) {
    struct dht22_data *data = dev_id;

    // num_edges = -1이면(측정 중이 아니면) 무시. 노이즈나 예상 밖 인터럽트로부터 배열을 보호하는 장치.
    if (data->num_edges >= 0 && data->num_edges < DHT22_EDGES_PER_READ) {
        data->edges[data->num_edges].ts = ktime_get_boottime_ns();
        data->edges[data->num_edges].value = gpiod_get_value(data->gpiod);
        data->num_edges++;

        // 배열 다 채워지면 complete() 호출
        if (data->num_edges >= DHT22_EDGES_PER_READ) {
            complete(&data->completion);
        }
    }

    return IRQ_HANDLED;
}

static int dht22_start_and_wait(struct dht22_data *data) {
    int ret;

    reinit_completion(&data->completion);
    // start pulse 중에도 혹시 노이즈로 인터럽트가 걸릴 가능성 대비 -> ISR이 이 시점부터 정상 기록하도록 미리 세팅
    data->num_edges = 0; // 이제부터 ISR이 기록 시작해도 됨

    // 1) GPIO를 출력모드로 전환 후 LOW로 끌어내림 (start signal)
    ret = gpiod_direction_output(data->gpiod, 0);
    if (ret) goto err;

    usleep_range(DHT22_START_LOW_US_MIN, DHT22_START_LOW_US_MAX);

    // 2) 다시 입력모드로 전환 -> 풀업이 라인을 HIGH로 올림
    // 이 시점부터 DHT22가 응답 시작
    ret = gpiod_direction_input(data->gpiod);
    if (ret) goto err;

    // 3) 인터럽트 등록 (양쪽 엣지 모두 캡처)
    ret = request_irq(data->irq, dht22_irq_handler, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING, "dht22", data);

    if (ret) goto err;

    // 4) ISR이 83개 엣지 다 채울 때까지 대기 (최대 1초)
    ret = wait_for_completion_killable_timeout(&data->completion, DHT22_WAIT_TIMEOUT_JIFFIES);

    free_irq(data->irq, data);

    if (ret < 0) goto err; // -ERESTARTSYS (signal에 의한 중단) - 이건 진짜 에러

    // ret == 0(타임아웃)이든 완료 신호를 받았든, 실제로 충분한 엣지가 잡혔으면 성공으로 취급
    if (data->num_edges < DHT22_DECODE_SPAN) {
        dev_err(data->dev, "dht22: only %d edges detected (need >= %d)\n",
                 data->num_edges, DHT22_DECODE_SPAN);        
        ret = -ETIMEDOUT;
        goto err;
    }

    return 0;

err:
    data->num_edges = -1; // 다시 "측정 중 아님" 상태로 복귀
    return ret;
}

// 타임스탬프 배열 -> 실제 온습도 값
static unsigned char dht22_decode_byte(const char *bits) {
    unsigned char ret = 0;
    int i;

    for (i = 0; i < 8; i++) {
        ret <<= 1;
        if (bits[i]) ret++;
    }
    return ret;
}

static int dht22_decode(struct dht22_data *data, int offset) {
    char bits[DHT22_BITS_PER_READ];
    unsigned char hum_int, hum_dec, temp_int, temp_dec, checksum;
    int i;
    s64 pulse_width;

    // edges[0], edges[1]은 preamble(센서의 80us LOW + 80us HIGH 응답)이므로 건너뜀
    // edges[2]부터 40비트 * 2엣지(LOW경계, HIGH경계) 페어로 데이터
    for (i = 0; i < DHT22_BITS_PER_READ; i++) {
        int low_idx = offset + 2 * i;
        int high_idx = offset + 2 * i + 1;

        if (!data->edges[low_idx].value) {
            return -EIO; // 이 offset으로는 정렬 안 맞음 -> 다른 offset 시도
        }

        pulse_width = data->edges[high_idx].ts - data->edges[low_idx].ts;
        bits[i] = pulse_width > DHT22_BIT_THRESHOLD_NS;
    }

    hum_int = dht22_decode_byte(&bits[0]);
    hum_dec = dht22_decode_byte(&bits[8]);
    temp_int = dht22_decode_byte(&bits[16]);
    temp_dec = dht22_decode_byte(&bits[24]);
    checksum = dht22_decode_byte(&bits[32]);

    if (((hum_int + hum_dec + temp_int + temp_dec) & 0xff) != checksum) {
        return -EIO;
    }

    // DHT22 계산식: 정수부(상위 8bit) + 소수부(하위 8bit) 결합
    // temp_int의 최상위 비트(0x80)가 1이면 음수 온도
    data->temperature = ((temp_int & 0x7f) << 8) + temp_dec;
    data->temperature *= (temp_int & 0x80) ? -1 : 1; // 0.1도 단위
    data->humidity = (hum_int << 8) + hum_dec; // 0.1% 단위
    data->last_read_ns = ktime_get_boottime_ns();

    return 0;
}

// sysfs show 함수 - 캐시 체크 + 측정 트리거
static int dht22_measure(struct dht22_data *data) {
    int ret, offset;

    mutex_lock(&data->lock);

    // 캐시가 아직 유효하면 재측정 없이 바로 리턴
    if (data->last_read_ns != 0 && ktime_get_boottime_ns() - data->last_read_ns < DHT22_CACHE_VALID_NS) {
        mutex_unlock(&data->lock);
        return 0;
    }

    ret = dht22_start_and_wait(data);
    if (ret) {
        mutex_unlock(&data->lock);
        return ret;
    }

    // offset을 뒤에서부터 줄여가며 정렬이 맞는 지점 탐색
    offset = data->num_edges - DHT22_DECODE_SPAN;
    ret = -EIO;
    for (; offset >= 0; offset--) {
        ret = dht22_decode(data, offset);
        if (!ret)
            break;
    }

    mutex_unlock(&data->lock);
    return ret;
}

static ssize_t temp_value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct dht22_data *data = dev_get_drvdata(dev);
    int ret = dht22_measure(data);

    if (ret) return ret;

    // 0.1도 단위 정수 -> "23.5" 형태로 출력
    return sysfs_emit(buf, "%d.%d\n", data->temperature / 10, abs(data->temperature % 10));
}

static ssize_t humid_value_show(struct device *dev, struct device_attribute *attr, char *buf) {
    struct dht22_data *data = dev_get_drvdata(dev);
    int ret = dht22_measure(data);

    if (ret) return ret;

    return sysfs_emit(buf, "%d.%d\n", data->humidity / 10, data->humidity % 10);
}

static DEVICE_ATTR_RO(temp_value);
static DEVICE_ATTR_RO(humid_value);

static struct attribute *dht22_attrs[] = {
    &dev_attr_temp_value.attr,
    &dev_attr_humid_value.attr,
    NULL,
};
ATTRIBUTE_GROUPS(dht22);

// platform_driver 등록 + probe 함수
static int dht22_probe(struct platform_device *pdev) {
    struct device *dev = &pdev->dev;
    struct dht22_data *data;

    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data) return -ENOMEM;

    data->dev = dev;
    data->gpiod = devm_gpiod_get(dev, NULL, GPIOD_IN);
    if (IS_ERR(data->gpiod)) {
        dev_err(dev, "failed to get gpio: %ld\n", PTR_ERR(data->gpiod));
        return PTR_ERR(data->gpiod);
    }

    data->irq = gpiod_to_irq(data->gpiod);
    if (data->irq < 0) {
        dev_err(dev, "gpio has no irq: %d\n", data->irq);
        return data->irq;
    }

    data->num_edges = -1; // 초기 상태: 측정 중 아님
    data->last_read_ns = 0; //캐시 없음 -> 첫 read 때 무조건 측정

    init_completion(&data->completion);
    mutex_init(&data->lock);

    platform_set_drvdata(pdev, data);

    dev_info(dev, "dht22 driver probed, irq=%d\n", data->irq);

    return 0;
}

static const struct of_device_id dht22_of_match[] = {
    { .compatible = "aosong,dht22" },
    { }
};
MODULE_DEVICE_TABLE(of, dht22_of_match);

static struct platform_driver dht22_driver = {
    .driver = {
        .name = "dht22",
        .of_match_table = dht22_of_match,
        .dev_groups = dht22_groups, // 5단계 ATTRIBUTE_GROUPS 매크로가 만든 것
    },
    .probe = dht22_probe,
};
module_platform_driver(dht22_driver);

MODULE_AUTHOR("Yuna Kim");
MODULE_DESCRIPTION("DHT22 temperature/humidity sensor driver");
MODULE_LICENSE("GPL v2");