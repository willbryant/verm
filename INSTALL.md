Installation
============

From source
-----------

You'll need Go 1.3 or later.  Build in the usual way for go programs:

```
go install github.com/willbryant/verm
```

This produces a standalone `$GOPATH/bin/verm` binary, which you can run from that
path or copy to your preferred system binary directory.

Or if you prefer a C-style build, manually checking out the git repository,
compiling, and installing:

```
git clone https://github.com/willbryant/verm.git
cd verm
make && sudo make install
```

This produces a standalone `verm` binary and installs it into `/usr/local/bin`.

Setup
-----

Verm runs on port 3404 by default, so you don't need it to be started as `root` -
so typically you should create a `verm` user and use that user to start the server.

Verm will store data in `/var/lib/verm`, so this needs to be writeable by that user.
