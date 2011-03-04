MHD_VERSION = 0.9.7
MHD = vendor/libmicrohttpd-$(MHD_VERSION)

CFLAGS += -D_GNU_SOURCE -pthread
CFLAGS += -I$(MHD) -I$(MHD)/src/include -I$(MHD)/src/include/plibc

verm: src/verm.o
	cc -o $@ $^ $(MHD)/src/daemon/.libs/libmicrohttpd.a -lcrypto

clean:
	rm -f src/verm.o verm
