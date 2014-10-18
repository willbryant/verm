package verm;

import "bytes"
import "crypto/sha256"
import "fmt"
import "hash"
import "io"
import "io/ioutil"
import "mime"
import "mimeext"
import "net/textproto"
import "net/http"
import "os"
import "path"
import "strings"

type fileUpload struct {
	root string
	path string
	location string
	content_type string
	encoding string
	input io.Reader
	hasher hash.Hash
	tempFile *os.File
}

func (server vermServer) UploadFile(w http.ResponseWriter, req *http.Request, replicating bool) (string, bool, error) {
	uploader, err := server.FileUploader(w, req, replicating)
	if err != nil {
		return "", false, err
	}
	defer uploader.Close()

	// read it in to the hasher
	_, err = io.Copy(uploader.hasher, uploader.input)
	if err != nil {
		return "", false, err
	}

	return uploader.Finish(server.Targets)
}

func (server vermServer) FileUploader(w http.ResponseWriter, req *http.Request, replicating bool) (*fileUpload, error) {
	// deal with '/..' etc.
	path := path.Clean(req.URL.Path)

	location := ""
	if replicating {
		location = path

		last_slash := strings.LastIndex(path, "/")
		path = path[0:last_slash - 3]
	}

	// don't allow uploads to the root directory itself, which would be unmanageable
	if len(path) <= 1 {
		path = DEFAULT_DIRECTORY_IF_NOT_GIVEN_BY_CLIENT
	}

	// make a tempfile in the requested (or default, as above) directory
	directory := server.RootDataDir + path
	err := os.MkdirAll(directory, DIRECTORY_PERMISSION)
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
	content_type := mediaTypeOrDefault(textproto.MIMEHeader(req.Header))
	if content_type == "multipart/form-data" {
		file, mpheader, mperr := req.FormFile(UPLOADED_FILE_FIELD)
		if mperr != nil {
			return nil, mperr
		}
		input = file
		content_type = mediaTypeOrDefault(mpheader.Header)
	}

	// as we read from the stream, copy it raw (without uncompressing) into the tempfile
	input = io.TeeReader(input, tempFile)

	// but uncompress the stream before feeding it to the hasher
	encoding := req.Header.Get("Content-Encoding")
	input, err = EncodingDecoder(encoding, input)
	if err != nil {
		return nil, err
	}

	return &fileUpload{
		root: server.RootDataDir,
		path: path,
		location: location,
		content_type: content_type,
		encoding: encoding,
		input: input,
		hasher: sha256.New(),
		tempFile: tempFile,
	}, nil
}

func (upload *fileUpload) Close() {
	upload.tempFile.Close();
}

func (upload *fileUpload) Finish(targets *ReplicationTargets) (string, bool, error) {
	// build the subdirectory and filename from the hash
	dir, dst := upload.encodeHash()

	// determine the appropriate extension from the content type
	extension := mimeext.ExtensionByType(upload.content_type)

	// create the directory
	subpath := upload.path + dir
	err := os.MkdirAll(upload.root + subpath, DIRECTORY_PERMISSION)
	if err != nil {
		return "", false, err
	}

	// compose the location
	location := upload.location

	if location == "" {
		location = fmt.Sprintf("%s%s%s", subpath, dst, extension)
	} else if !strings.HasPrefix(location, subpath + dst) ||
			  strings.Contains(location[len(subpath) + len(dst):], "/") {
		// can't recreate the path; this is effectively a checksum failure
		return "", false, &WrongLocationError{location}
	}

	// hardlink the file into place
	new_file := true
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
			return "", false, err
		}

		// check the file contents match
		existing, openerr := os.Open(filename)
		if openerr != nil {
			// can't open the existing file - may not be a regular file, or not accessible to us; return the original error
			return "", false, err
		}
		defer existing.Close()
		upload.tempFile.Seek(0, 0)

		if sameContents(upload.tempFile, existing) {
			// success
			new_file = false
			break
		}

		// contents don't match, which in practice means corruption since the chance of finding a sha256 hash collision is low! but we assume the best and use a suffix on the filename
		attempt++
		location = fmt.Sprintf("%s%s_%d%s", subpath, dst, attempt, extension)
	}

	os.Remove(upload.tempFile.Name()) // ignore errors, the tempfile is moot at this point

	targets.Enqueue(ReplicationJob{location: location, filename: upload.root + location, content_type: upload.content_type, encoding: upload.encoding})

	return location, new_file, nil
}

func (upload *fileUpload) encodeHash() (string, string) {
	const encode_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
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
	dir.WriteByte(encode_chars[((md[0] & 0xf8) >> 3)])
	dir.WriteByte(encode_chars[((md[0] & 0x07) << 3) + ((md[1] & 0xe0) >> 5)])

	var dst bytes.Buffer
	dst.WriteByte('/') // we put each file in a subdirectory off the main root, whose name is the first two characters of the hash.
	dst.WriteByte(encode_chars[((md[1] & 0x1f))])

	for srcindex := 2; srcindex < len(md); srcindex += 3 {
		s0 := md[srcindex + 0]
		s1 := md[srcindex + 1] // thanks to the above, we know that we have an exact
		s2 := md[srcindex + 2] // multiple of 3 bytes left to encode in this loop
		dst.WriteByte(encode_chars[((s0 & 0xfc) >> 2)])
		dst.WriteByte(encode_chars[((s0 & 0x03) << 4) + ((s1 & 0xf0) >> 4)])
		dst.WriteByte(encode_chars[((s1 & 0x0f) << 2) + ((s2 & 0xc0) >> 6)])
		dst.WriteByte(encode_chars[((s2 & 0x3f))])
	}

	return dir.String(), dst.String()
}

func mediaTypeOrDefault(header textproto.MIMEHeader) string {
	media_type, _, err := mime.ParseMediaType(header.Get("Content-Type"))
	if err != nil {
		return "application/octet-stream"
	}
	return media_type
}

func sameContents(file1, file2 io.Reader) bool {
	var contents1 = make([]byte, 65536)
	var contents2 = make([]byte, 65536)
	for {
		// try to read some bytes from file1, then try to read the same number of bytes (exactly) from file2
		len1, err1 := file1.Read(contents1)

		if len1 > 0 {
			len2, err2 := io.ReadFull(file2, contents2[0:len1])

			if err2 != nil || !bytes.Equal(contents1[0:len1], contents2[0:len2]) {
				// hit an error or premature EOF (ie. file lengths didn't match), or the file contents weren't
				// the same, so again failed/conflicted
				return false
			}

		} else if err1 == io.EOF {
			// normal EOF on the first file; check the second file is at EOF too - we can't use the normal
			// ReadFull code above and just test err2 because ReadFull with a 0-long slice is a noop and will
			// always return no error, so read a 1-long slice instead and see what happens
			len2, err2 := file2.Read(contents2[0:1])

			// if we reached the end of one file but not the other (or had another read error from it),
			// return a failure/conflict; otherwise return success
			return len2 == 0 && err2 == io.EOF

		// "Implementations of Read are discouraged from returning a zero byte count with a nil error,
		// and callers should treat that situation as a no-op."
		} else if err1 != nil {
			// hit an error, treated as failed/conflict
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
