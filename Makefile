default: verm

install: verm
	install $^ /usr/local/bin
	mkdir -p /var/lib/verm

verm: *.go */*.go
	GOPATH=`pwd`/../.. go build

clean:
	rm -f verm

test_verm: verm
	BUNDLE_GEMFILE=test/Gemfile bundle exec testrb test/get_files_test.rb test/create_files_multipart_test.rb test/create_files_raw_test.rb test/health_check_test.rb test/replication_put_test.rb test/replication_missing_test.rb test/replication_propagation_test.rb test/replication_topology_test.rb test/read_forwarding_test.rb
