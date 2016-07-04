PREFIX    = /usr/local
BINDIR    = $(PREFIX)/bin

default: verm

install: verm
	install -d $(DESTDIR)$(BINDIR)/
	install $^ $(DESTDIR)$(BINDIR)
	mkdir -p $(DESTDIR)/var/lib/verm

verm: *.go */*.go
	mkdir -p src/github.com/willbryant
	( cd src/github.com/willbryant && ln -sf ../../../ verm)
	GOPATH="`pwd`" go build

clean:
	rm -rf verm src

test_verm: verm
	BUNDLE_GEMFILE=test/Gemfile bundle exec testrb test/get_files_test.rb test/create_files_multipart_test.rb test/create_files_raw_test.rb test/health_check_test.rb test/replication_put_test.rb test/replication_missing_test.rb test/replication_propagation_test.rb test/replication_topology_test.rb test/read_forwarding_test.rb
