package verm

import "compress/gzip"
import "io"

func EncodingDecoder(encoding string, input io.Reader) (io.Reader, error) {
	switch encoding {
	case "":
		return input, nil

	case "gzip":
		return gzip.NewReader(input)

	default:
		return nil, &EncodingError{encoding: encoding}
	}
}

func EncodingSuffix(encoding string) string {
	switch encoding {
	case "gzip":
		return ".gz"

	default:
		return ""
	}
}

type EncodingError struct {
	encoding string
}

func (e *EncodingError) Error() string {
	return "Don't know how to decode " + e.encoding
}
