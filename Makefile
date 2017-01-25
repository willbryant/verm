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
	BUNDLE_GEMFILE=test/Gemfile bundle exec rake -f test/Rakefile test
