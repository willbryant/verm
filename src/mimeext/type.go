// based on https://golang.org/src/pkg/mime/type_unix.go,
// which unfortunately does not export loadMimeFile,
// and https://golang.org/src/pkg/mime/type.go, which
// unfortunately does not implement ExtensionByType

// Copyright 2010 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package mimeext

import (
	"bufio"
	"fmt"
	"mime"
	"os"
	"strings"
	"sync"
)

/* a somewhat-arbitrary selection of the most important standard mime types for use with net apps.
   excludes all non-standard or vendor-specific types and most non-document types.  should generally
   be supplemented by your /etc/mime.types file, especially if you plan to store audio, video, or
   animation files or source documents from word processors & spreadsheets etc. */

var mimeTypes = map[string]string{
	".css":   "text/css; charset=utf-8",
	".csv":   "text/csv; charset=utf-8",
	".eml":   "message/rfc822",
	".gif":   "image/gif",
	".gz":    "application/gzip", // strictly speaking under MIME gzip would only be used as an encoding, not a content-type, but it's common to have .gz files
	".htm":   "text/html; charset=utf-8",
	".html":  "text/html; charset=utf-8",
	".jpg":   "image/jpeg",
	".jpeg":  "image/jpeg",
	".js":    "application/javascript",
	".json":  "application/json",
	".pdf":   "application/pdf",
	".png":   "image/png",
	".svg":   "image/svg+xml",
	".tar":   "application/tar",
	".txt":   "text/plain",
	".tsv":   "text/tab-separated-values",
	".xhtml": "application/xhtml+xml",
	".xml":   "text/xml; charset=utf-8",
	".xsl":   "text/xml; charset=utf-8",
	".xsd":   "text/xml; charset=utf-8",
	".zip":   "application/zip",
}

var mimeExtensions = map[string]string{
	"application/pdf": ".pdf",
	"application/javascript": ".js",
	"application/x-javascript": ".js",
	"application/json": ".json",
	"application/gzip": ".gz", // strictly speaking under MIME gzip would only be used as an encoding, not a content-type, but it's common to have .gz files
	"application/x-gzip": ".gz",
	"application/tar": ".tar",
	"application/xhtml+xml": ".xhtml",
	"application/zip": ".zip",
	"image/gif": ".gif",
	"image/jpeg": ".jpg",
	"image/png": ".png",
	"image/svg+xml": ".svg",
	"text/comma-separated-values": ".csv",
	"text/css": ".css",
	"text/csv": ".csv",
	"text/html": ".html",
	"text/plain": ".txt",
	"text/tab-separated-values": ".tsv",
	"text/xml": ".xml",
	"message/rfc822": ".eml",
}

var mimeLock sync.RWMutex

var once sync.Once

// TypeByExtension returns the MIME type associated with the file extension ext.
// The extension ext should begin with a leading dot, as in ".html".
// When ext has no associated type, TypeByExtension returns "".
//
// The built-in table is small but it is augmented by calling LoadMimeFile.
//
// Text types have the charset parameter set to "utf-8" by default.
func TypeByExtension(ext string) string {
	mimeLock.RLock()
	typename := mimeTypes[ext]
	mimeLock.RUnlock()
	return typename
}

// TypeByExtension returns the extension associated with the MIME type typ.
// The extension ext should begin with a leading dot, as in ".html".
// When ext has no associated type, TypeByExtension returns "".
//
// The built-in table is small but it is augmented by calling LoadMimeFile.
func ExtensionByType(typ string) string {
	mimeLock.RLock()
	extension := mimeExtensions[typ]
	mimeLock.RUnlock()
	return extension
}

// AddExtensionType sets the MIME type associated with
// the extension ext to typ.  The extension should begin with
// a leading dot, as in ".html".
func AddExtensionType(ext, typ string) error {
	if ext == "" || ext[0] != '.' {
		return fmt.Errorf(`mime: extension "%s" misses dot`, ext)
	}
	return setExtensionType(ext, typ)
}

// AddExtensionType sets the extension associated associated with
// the MIME type typ to ext.  The extension should begin with
// a leading dot, as in ".html".
func AddTypeExtension(ext, typ string) error {
	if ext == "" || ext[0] != '.' {
		return fmt.Errorf(`mime: extension "%s" misses dot`, ext)
	}
	return setTypeExtension(ext, typ)
}

func setExtensionType(extension, mimeType string) error {
	_, param, err := mime.ParseMediaType(mimeType)
	if err != nil {
		return err
	}
	if strings.HasPrefix(mimeType, "text/") && param["charset"] == "" {
		param["charset"] = "utf-8"
		mimeType = mime.FormatMediaType(mimeType, param)
	}
	mimeLock.Lock()
	mimeTypes[extension] = mimeType
	mimeLock.Unlock()
	return nil
}

func setTypeExtension(extension, mimeType string) error {
	mimeLock.Lock()
	mimeExtensions[mimeType] = extension
	mimeLock.Unlock()
	return nil
}

func LoadMimeFile(filename string) {
	f, err := os.Open(filename)
	if err != nil {
		return
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		fields := strings.Fields(scanner.Text())
		if len(fields) <= 1 || fields[0][0] == '#' {
			continue
		}
		mimeType := fields[0]
		if fields[1][0] != '#' {
			AddTypeExtension("."+fields[1], mimeType)
		}
		for _, ext := range fields[1:] {
			if ext[0] == '#' {
				break
			}
			AddExtensionType("."+ext, mimeType)
		}
	}
	if err := scanner.Err(); err != nil {
		panic(err)
	}
}
