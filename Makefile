MHD_VERSION = 0.9.7
MHD = vendor/libmicrohttpd-$(MHD_VERSION)

CFLAGS += -D_GNU_SOURCE -pthread
CFLAGS += -I$(MHD) -I$(MHD)/src/include -I$(MHD)/src/include/plibc

default: verm

install: verm
	install $^ /usr/local/bin

$(MHD)/src/daemon/.libs/libmicrohttpd.a:
	cd $(MHD) && ./configure --disable-https && make

verm: $(MHD)/src/daemon/.libs/libmicrohttpd.a src/verm.o
	cc -o $@ $^ -lcrypto

clean:
	rm -f src/verm.o verm
	cd $(MHD) && make distclean
