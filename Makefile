MHD = vendor/libmicrohttpd
MHD_LIBRARY = $(MHD)/src/daemon/.libs/libmicrohttpd.a

CFLAGS += -D_GNU_SOURCE -g
CFLAGS += -I$(MHD) -I$(MHD)/src/include -I$(MHD)/src/include/plibc -Ivendor/khash
LDFLAGS += -lpthread -lcrypto -lz

PLATFORM := $(shell uname -s)
ifeq ($(PLATFORM),SunOS)
	CFLAGS += -pthreads -D_POSIX_PTHREAD_SEMANTICS -DNEED_SA_LEN
	LDFLAGS += -lsocket
	MHDFLAGS = LIBS="-lsocket -lnsl"
else
	ifeq ($(PLATFORM),Linux)
		CFLAGS += -DNEED_SA_LEN
	endif
	CFLAGS += -pthread
endif

CFLAGS += -std=c99 -Wall -pedantic

default: verm

install: verm
	install $^ /usr/local/bin

$(MHD_LIBRARY):
	cd $(MHD) && ./configure --disable-https $(MHDFLAGS) && make

$(MHD)/MHD_config.h: $(MHD_LIBRARY)

verm: $(MHD)/MHD_config.h src/verm.o src/responses.o src/response_headers.o src/mhd_patches.o src/response_logging.o src/statistics_reports.o src/replication.o src/decompression.o src/str.o src/mime_types.o $(MHD_LIBRARY)
	cc $(LDFLAGS) -o $@ $^

clean_verm:
	rm -f src/*.o verm

clean: clean_verm
	cd $(MHD) && make distclean

test_verm: verm
	testrb test/get_files_test.rb test/create_files_multipart_test.rb test/create_files_raw_test.rb test/replication_put_test.rb test/replication_propagation_test.rb
