package verm;

import "bytes"
import "compress/gzip"
import "crypto/sha256"
import "hash"
import "io"
import "io/ioutil"
import "mime"
import "mimeext"
import "net/textproto"
import "net/http"
import "os"
import "path"

type fileUpload struct {
	root string
	path string
	content_type string
	gzipped bool
	input io.Reader
	hasher hash.Hash
	tempFile *os.File
}

const directory_permission = 0777
const default_directory_if_not_given_by_client = "/default"
const uploaded_file_field = "uploaded_file"

func (server vermServer) UploadFile(w http.ResponseWriter, req *http.Request) (string, bool, error) {
	uploader, err := server.FileUploader(w, req)
	if err != nil {
		return "", false, err
	}
	defer uploader.Close()

	// read it in to the hasher
	_, err = io.Copy(uploader.hasher, uploader.input)
	if err != nil {
		return "", false, err
	}

	return uploader.Finish()
}

func (server vermServer) FileUploader(w http.ResponseWriter, req *http.Request) (*fileUpload, error) {
	// deal with '/..' etc.
	path := path.Clean(req.URL.Path)

	// don't allow uploads to the root directory itself, which would be unmanageable
	if len(path) <= 1 {
		path = default_directory_if_not_given_by_client
	}

	// make a tempfile in the requested (or default, as above) directory
	directory := server.RootDataDir + path
	err := os.MkdirAll(directory, directory_permission)
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
		file, mpheader, mperr := req.FormFile(uploaded_file_field)
		if mperr != nil {
			return nil, mperr
		}
		input = file
		content_type = mediaTypeOrDefault(mpheader.Header)
	}

	// as we read from the stream, copy it raw (without uncompressing) into the tempfile
	input = io.TeeReader(input, tempFile)

	// but uncompress the stream before feeding it to the hasher
	gzipped := req.Header.Get("Content-Encoding") == "gzip"
	if gzipped {
		input, err = gzip.NewReader(input)
		if err != nil {
			return nil, err
		}
	}

	return &fileUpload{
		root: server.RootDataDir,
		path: path,
		content_type: content_type,
		gzipped: gzipped,
		input: input,
		hasher: sha256.New(),
		tempFile: tempFile,
	}, nil
}

func (upload *fileUpload) Close() {
	upload.tempFile.Close();
}

func (upload *fileUpload) Finish() (string, bool, error) {
	// build the subdirectory and filename from the hash
	dir, dst := upload.encodeHash()

	// determine the appropriate extension from the content type
	extension := mimeext.ExtensionByType(upload.content_type)

	// create the directory
	subpath := upload.path + dir
	err := os.MkdirAll(upload.root + subpath, directory_permission)
	if err != nil {
		return "", false, err
	}

	// compose the filename; if the upload was itself compressed, tack on the gzip suffix -
	// but note that this changes only the filename and not the returned location
	location := subpath + dst + extension
	filename := upload.root + location

	if upload.gzipped {
		filename += ".gz"
	}

	// hardlink the file into place
	new_file := true
	err = os.Link(upload.tempFile.Name(), filename)
	if err != nil {
		// this most often means the path already exists
		if !os.IsExist(err) {
			return "", false, err
		}

		// check it's a regular file
		stat, staterr := os.Lstat(filename)

		if staterr != nil || !stat.Mode().IsRegular() {
			// no, can't stat or isn't a file, so return the original error
			return "", false, err
		}
		// TODO: check file contents match?
		new_file = false
	}

	os.Remove(upload.tempFile.Name()) // ignore errors, the tempfile is moot at this point

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
