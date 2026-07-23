# 커널 모듈 이름 (ads1115_driver.c -> ads1115_driver.ko)
obj-m += ads1115_driver.o

# 라즈베리파이 커널 헤더 경로
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all clean load unload

# 커널 모듈 빌드
all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

# 모듈 로드/언로드
load:
	sudo insmod ads1115_driver.ko

unload:
	sudo rmmod ads1115_driver