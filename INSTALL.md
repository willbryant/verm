Installation
============

From source
-----------

You'll need go 1.3 or later.

```
git clone https://github.com/willbryant/verm.git
cd verm
make && sudo make install
```

This produces a standalone `verm` binary and installs it into `/usr/local/bin`.

There's no need to run verm as `root`, so typically you should create a `verm` user.

Verm will store data in `/var/lib/verm`, so this needs to be writeable by that user.
