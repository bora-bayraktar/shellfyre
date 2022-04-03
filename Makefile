obj-m := pstraverse.o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.SILENT:

default: module gcc run

module:
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules	

clean: 
	$(MAKE) -C $(KDIR) M=$(shell pwd) clean
	rm main

gcc:
	gcc -o main shellfyre.c

run:
	sudo ./main
