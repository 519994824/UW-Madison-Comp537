CC=gcc
CFLAGS-common=-Wall -Wextra -Werror -pedantic -std=gnu18
CFLAGS=$(CFLAGS-common) -O2
CFLAGS-dbg=$(CFLAGS-common) -Og -ggdb -g
TARGET=wsh
SRC=$(TARGET).c
SRC-HEAD=$(TARGET).h
TARGET-TEST=test
SRC-TEST=$(TARGET-TEST).c
LOGIN=qinxinghao
SUBMITPATH=~cs537-1/handin/$(LOGIN)/p3
HOST=rockhopper-01.cs.wisc.edu

.PHONY:all clean submit test

all: $(TARGET) $(TARGET)-dbg

$(TARGET): $(SRC) $(SRC-HEAD)
	$(CC) $(CFLAGS) $< -o $@

$(TARGET)-dbg: $(SRC) $(SRC-HEAD)
	$(CC) $(CFLAGS-dbg) $< -o $@

clean:
	rm -rf $(TARGET) $(TARGET)-dbg

submit:
	scp -r ../ $(LOGIN)@$(HOST):$(SUBMITPATH)

