// based on https://golang.org/src/pkg/mime/type_unix.go,
// which unfortunately does not export loadMimeFile

// Copyright 2010 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

// +build darwin dragonfly freebsd linux nacl netbsd openbsd solaris

package verm

import (
	"bufio"
	"mime"
	"os"
	"strings"
)

func loadMimeFile(filename string) {
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
		for _, ext := range fields[1:] {
			if ext[0] == '#' {
				break
			}
			mime.AddExtensionType("."+ext, mimeType)
		}
	}
	if err := scanner.Err(); err != nil {
		panic(err)
	}
}
