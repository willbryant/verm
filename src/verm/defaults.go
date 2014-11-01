package verm

const DEFAULT_ROOT = "/var/lib/verm"
const DEFAULT_LISTEN_ADDRESS = "0.0.0.0"
const DEFAULT_VERM_PORT = "1138"
const DEFAULT_MIME_TYPES_FILE = "/etc/mime.types"

const DIRECTORY_PERMISSION = 0777
const DEFAULT_DIRECTORY_IF_NOT_GIVEN_BY_CLIENT = "/default"
const UPLOADED_FILE_FIELD = "uploaded_file"
const REPLICATION_BACKLOG = 10000
const BACKOFF_BASE_TIME = 1
const BACKOFF_MAX_TIME = 60

const MISSING_FILES_PATH = "/_missing"
const MISSING_FILES_BATCH_SIZE = 256*1024 // bytes, but only approximate