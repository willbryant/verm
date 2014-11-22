Verm
====

Verm is a WORM (write-once, read-many) file store to make it easy to reliably
store and replicate files.

Use case
--------

* You need to store files on your servers
* You want to replicate them between machines and datacentres, for redundancy
* You need it to be very easy to bring up new replicas in the cluster without
  complicated resynchronisation procedures.
* You keep business process metadata about the files in your database, but you
  don't want to bloat your database with the file data itself
* You don't need to change the original files once created, and may even need
  to ensure that files are not changed or deleted, for auditability
* Simplicity and robustness is a priority; no complicated architectures with
  many moving parts

Design
------
* Provides only immutable file storage
* Files are content-addressed based on the SHA-256 of the file data, so storing
  the same file twice stores only one copy
* Orthogonal to metadata concerns - not a database
* Stores files in local storage for easy ops and backup
* Replicates all files to other Verm cluster members for machine-level RAID
* Simple HTTP interface: simply POST file data to the path you want to store
  the files under with the appropriate content-type
* Automatically creates directories as required
* Follows standard file-serving conventions for other webservers: MIME types
  are mapped to filename extensions and gzip encoding to .gz extensions
* Automatically compresses and decompresses as required based on request URLs
* Client access by HTTP GET and POST, easy to use from any language
* Replication by HTTP PUT, easy to transport through datacentre firewalls and
  proxies

Installation
------------

See [the install guide](INSTALL.md).

Use
---

Use any language's HTTP client library to make POST requests to the path you
want to store the file under, for example

```
POST /2019/los_angeles/docs
Content-type: image/png

<raw file content>
```

Verm will automatically create the 2014, los_angeles, and docs subdirectories
under the root data directory (`/var/lib/verm` by default), hash the file data,
turn the hash into a filename using URL-safe characters, and return the path
in a Location header with a 204 Created response.

The file will immediately be replicated to other Verm servers.  (If Verm is
restarted before the file is replicated, it will still be replicated because
Verm resynchronises file lists after restart, sending any files locally present
that are not on other servers).

GET requests are usually served by Verm itself, but because Verm will also
choose an appropriate extension for the file, you can also serve files using
any regular webserver if you prefer, making it easy to migrate to or from Verm.

As a concession to tools that don't cope well with huge numbers of entries in
single directories, Verm will place files under subdirectories of the requested
path based on the first bits of the file content hash.

Compression support is intended to be transparent to the client.  If file data
is posted in gzip-encoded, the file will be stored with a `.gz` suffix for
compatibility with other webservers, but the path returned will be without the
`.gz` suffix.  Requests for this URL will therefore serve the file with a gzip
content-encoding and the original content-type rather than as a gzip file; if
the client declares that it does not support the gzip content-encoding, Verm
will decompress the file for the client.
