MHD_VERSION = 0.9.7
MHD = vendor/libmicrohttpd-$(MHD_VERSION)
MHD_LIBRARY = $(MHD)/src/daemon/.libs/libmicrohttpd.a

CFLAGS += -D_GNU_SOURCE
CFLAGS += -I$(MHD) -I$(MHD)/src/include -I$(MHD)/src/include/plibc -Ivendor/khash
LDFLAGS += -lpthread -lcrypto

PLATFORM := $(shell uname -s)
ifeq ($(PLATFORM),SunOS)
	CFLAGS += -pthreads
	LDFLAGS += -lsocket
	MHDFLAGS = LIBS="-lsocket -lnsl"
else
	CFLAGS += -pthread
endif

default: verm

install: verm
	install $^ /usr/local/bin

$(MHD_LIBRARY):
	cd $(MHD) && ./configure --disable-https $(MHDFLAGS) && make

$(MHD)/MHD_config.h: $(MHD_LIBRARY)

verm: $(MHD)/MHD_config.h src/verm.o src/str.o src/mime_types.o $(MHD_LIBRARY)
	cc $(LDFLAGS) -o $@ $^

clean:
	rm -f src/verm.o verm
	cd $(MHD) && make distclean

test_verm: verm
	testrb test/create_files_test.rb test/get_files_test.rb
