# 커널 모듈 이름 (dht22_driver.c -> dht22_driver.ko)
obj-m += dht22_driver.o

# 라즈베리파이 커널 헤더 경로 (메모리에 확인된 커널 버전 기준)
KDIR := /lib/modules/6.18.34+rpt-rpi-v8/build
PWD := $(shell pwd)

# Device Tree overlay 관련
DTS := ../../dts/dht22-overlay.dts
DTBO := dht22.dtbo
OVERLAY_DIR := /boot/firmware/overlays

.PHONY: all clean load unload dt-apply dt-remove

# 커널 모듈 빌드
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# 모듈 로드/언로드
load:
	sudo insmod dht22_driver.ko

unload:
	sudo rmmod dht22_driver

# DT overlay 컴파일 + 적용/해제
dt-apply:
	dtc -@ -I dts -O dtb -o $(DTBO) $(DTS)
	sudo cp $(DTBO) $(OVERLAY_DIR)/
	sudo dtoverlay dht22

dt-remove:
	sudo dtoverlay -r dht22