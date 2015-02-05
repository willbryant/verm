package main

import "bytes"
import "compress/gzip"
import "crypto/sha256"
import "fmt"
import "hash"
import "io"
import "io/ioutil"
import "mime"
import "net/textproto"
import "net/http"
import "os"
import "path"
import "strings"
import "verm/mimeext"

type fileUpload struct {
	root        string
	path        string
	location    string
	contentType string
	extension   string
	encoding    string
	input       io.Reader
	hasher      hash.Hash
	tempFile    *os.File
}

func (server vermServer) UploadFile(w http.ResponseWriter, req *http.Request, replicating bool) (location string, newFile bool, err error) {
	uploader, err := server.FileUploader(w, req, replicating)
	if err != nil {
		return
	}
	defer uploader.Close()

	// read it in to the hasher
	_, err = io.Copy(uploader.hasher, uploader.input)
	if err != nil {
		return
	}

	return uploader.Finish(server.Targets)
}

func (server vermServer) FileUploader(w http.ResponseWriter, req *http.Request, replicating bool) (*fileUpload, error) {
	// deal with '/..' etc.
	path := path.Clean(req.URL.Path)

	location := ""
	if replicating {
		location = path
		path = path[0 : strings.LastIndex(path, "/")-3]
	}

	// don't allow uploads to the root directory itself, which would be unmanageable
	if len(path) <= 1 {
		path = DefaultDirectoryIfNotGivenByClient
	}

	// make a tempfile in the requested (or default, as above) directory
	directory := server.RootDataDir + path
	err := os.MkdirAll(directory, DirectoryPermission)
	if err != nil {
		return nil, err
	}

	var tempFile *os.File
	tempFile, err = ioutil.TempFile(directory, "_upload")
	if err != nil {
		return nil, err
	}

	// if the upload is a raw post, the input stream is the request body
	var input io.Reader = req.Body

	// but if the upload is a browser form, the input stream needs multipart decoding
	contentType := mediaTypeOrDefault(textproto.MIMEHeader(req.Header))
	if contentType == "multipart/form-data" {
		file, mpheader, mperr := req.FormFile(UploadedFieldFieldForMultipart)
		if mperr != nil {
			return nil, mperr
		}
		input = file
		contentType = mediaTypeOrDefault(mpheader.Header)
	}

	// determine the appropriate extension from the content type
	extension := mimeext.ExtensionByType(contentType)

	// if the file is both gzip-encoded and is actually a gzip file itself, strip the redundant encoding
	storageEncoding := req.Header.Get("Content-Encoding")
	if extension == ".gz" && storageEncoding != "" {
		input, err = EncodingDecoder(storageEncoding, input)
		if err != nil {
			return nil, err
		}
		storageEncoding = ""
	}

	// as we read from the stream, copy it into the tempfile - potentially in encoded format (except for the above case)
	input = io.TeeReader(input, tempFile)

	// but uncompress the stream before feeding it to the hasher
	input, err = EncodingDecoder(storageEncoding, input)
	if err != nil {
		return nil, err
	}

	// in addition to handling gzip content-encoding, if an actual .gz file is uploaded,
	// we need to decompressed it and hash its contents rather than the raw file itself;
	// otherwise there would be an ambiguity between application/octet-stream files with
	// gzip on-disk compression and application/gzip files with no compression, and they
	// would appear to have different hashes, which would break replication
	if extension == ".gz" {
		input, err = gzip.NewReader(input)
		if err != nil {
			return nil, err
		}
	}

	return &fileUpload{
		root:        server.RootDataDir,
		path:        path,
		location:    location,
		contentType: contentType,
		extension:   extension,
		encoding:    storageEncoding,
		input:       input,
		hasher:      sha256.New(),
		tempFile:    tempFile,
	}, nil
}

func (upload *fileUpload) Close() {
	upload.tempFile.Close()
}

func (upload *fileUpload) Finish(targets *ReplicationTargets) (location string, newFile bool, err error) {
	// build the subdirectory and filename from the hash
	dir, dst := upload.encodeHash()

	// create the directory
	subpath := upload.path + dir
	err = os.MkdirAll(upload.root + subpath, DirectoryPermission)
	if err != nil {
		return
	}

	// compose the location
	location = upload.location

	if location == "" {
		location = fmt.Sprintf("%s%s%s", subpath, dst, upload.extension)
	} else if !strings.HasPrefix(location, subpath + dst) ||
			  strings.Contains(location[len(subpath) + len(dst):], "/") {
		// can't recreate the path; this is effectively a checksum failure
		err = &WrongLocationError{location}
		return
	}

	err = upload.tempFile.Sync()
	if err != nil {
		return
	}

	// hardlink the file into place
	newFile = true
	attempt := 1
	for {
		// compose the filename; if the upload was itself compressed, tack on the gzip suffix -
		// but note that this changes only the filename and not the returned location
		filename := upload.root + location + EncodingSuffix(upload.encoding)

		err = os.Link(upload.tempFile.Name(), filename)
		if err == nil {
			// success
			break
		}

		// the most common error is that the path already exists, which would be normal if it's the same file, but any other error is definitely an error
		if !os.IsExist(err) {
			// some other error, return it
			return
		}

		// check the file contents match
		existing, openerr := os.Open(filename)
		if openerr != nil {
			// can't open the existing file - may not be a regular file, or not accessible to us; return the original error
			return
		}
		defer existing.Close()
		upload.tempFile.Seek(0, 0)

		if sameDecodedContents(upload.tempFile, existing, upload.encoding) {
			// success
			newFile = false
			break
		}

		// contents don't match, which in practice means corruption since the chance of finding a sha256 hash collision is low! but we assume the best and use a suffix on the filename
		attempt++
		location = fmt.Sprintf("%s%s_%d%s", subpath, dst, attempt, upload.extension)
	}

	os.Remove(upload.tempFile.Name()) // ignore errors, the tempfile is moot at this point

	// try to fsync the directory too
	dirnode, openerr := os.Open(upload.root + subpath)
	if openerr == nil { // ignore if not allowed to open it
		dirnode.Sync();
		dirnode.Close();
	}

	if newFile {
		if upload.extension == ".gz" {
			// for the sake of replication, we can treat it as a gzip-encoded binary file rather than a raw gzip file;
			// this is how we will interpret the filename when we restart and resync, so it's better to always do this
			targets.EnqueueJob(location[:len(location) - len(upload.extension)])
		} else {
			targets.EnqueueJob(location)
		}
	}

	err = nil
	return
}

func (upload *fileUpload) encodeHash() (string, string) {
	const encodingAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
	md := upload.hasher.Sum(nil)

	// we have 256 bits to encode, which is not an integral multiple of the 6 bits per character we can encode with
	// base64.  if we run normal base64, we end up with bits free at the end of the string.  we have chosen to
	// use the free bits at the start of the directory name and file name instead, to avoid needing to use the -
	// character (and with it, the last 31 characters in our alphabet) in the first character of these path
	// components, so making our filenames 'nicer' in that even in the unusual scenario where an admin or user is
	// in the directory and referring to the subdirectories or files on the command line without qualifying them
	// (using ./ or the full path name), there is no chance of them being interpreted as command-line option switches.
	// of course, we'd rather not use - in our alphabet, but the only alternatives get URL-encoded, which is worse.
	// so we use 5 bits in the first character, the next 6 bits in the second character, then after the /, the next 5
	// bits in the last special character, so encoding exactly 2 input bytes in 3 output bytes plus one more for the /.
	// for 32 input bytes, we will need 45 output bytes (3 bytes for the first 2 input bytes, 1 byte for the /, then
	// 30*4/3=40 bytes for the remaining input bytes, one for the NUL termination byte)
	var dir bytes.Buffer
	dir.WriteByte('/')
	dir.WriteByte(encodingAlphabet[((md[0] & 0xf8) >> 3)])
	dir.WriteByte(encodingAlphabet[((md[0] & 0x07) << 3) + ((md[1] & 0xe0) >> 5)])

	var dst bytes.Buffer
	dst.WriteByte('/')
	dst.WriteByte(encodingAlphabet[((md[1] & 0x1f))])

	for srcindex := 2; srcindex < len(md); srcindex += 3 {
		s0 := md[srcindex + 0]
		s1 := md[srcindex + 1] // thanks to the above, we know that we have an exact
		s2 := md[srcindex + 2] // multiple of 3 bytes left to encode in this loop
		dst.WriteByte(encodingAlphabet[((s0 & 0xfc) >> 2)])
		dst.WriteByte(encodingAlphabet[((s0 & 0x03) << 4) + ((s1 & 0xf0) >> 4)])
		dst.WriteByte(encodingAlphabet[((s1 & 0x0f) << 2) + ((s2 & 0xc0) >> 6)])
		dst.WriteByte(encodingAlphabet[((s2 & 0x3f))])
	}

	return dir.String(), dst.String()
}

func mediaTypeOrDefault(header textproto.MIMEHeader) string {
	mediaType, _, err := mime.ParseMediaType(header.Get("Content-Type"))
	if err != nil {
		return "application/octet-stream"
	}
	return mediaType
}

func sameDecodedContents(stream1, stream2 io.Reader, encoding string) bool {
	decodedStream1, err := EncodingDecoder(encoding, stream1)
	if err != nil {
		return false
	}

	decodedStream2, err := EncodingDecoder(encoding, stream2)
	if err != nil {
		return false
	}

	return sameContents(decodedStream1, decodedStream2)
}

func sameContents(stream1, stream2 io.Reader) bool {
	var contents1 = make([]byte, 65536)
	var contents2 = make([]byte, 65536)
	for {
		// try to read some bytes from stream1, then try to read the same number of bytes (exactly) from stream2
		len1, err1 := io.ReadFull(stream1, contents1)
		len2, err2 := io.ReadFull(stream2, contents2)

		if len1 == 0 && len2 == 0 {
			// if we reached EOF without encountering any differences, the files match
			return err1 == io.EOF && err2 == io.EOF
		}

		if len1 != len2 || !bytes.Equal(contents1[0:len1], contents2[0:len2]) ||
			(err1 != nil && err1 != io.ErrUnexpectedEOF) ||
			(err2 != nil && err2 != io.ErrUnexpectedEOF) {
			// file lengths didn't match, the file contents weren't the same, or we hit an error, so failed/conflicted
			return false
		}
	}
}

type WrongLocationError struct {
	location string
}

func (e *WrongLocationError) Error() string {
	return e.location + " is not the correct location, is the file corrupt?"
}
