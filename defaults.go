package main

const DefaultRoot = "/var/lib/verm"
const DirectoryPermission = 0777

const DefaultListenAddress = "0.0.0.0"
const DefaultPort = "1138"

const DefaultMimeTypesFile = "/etc/mime.types"
const DefaultDirectoryIfNotGivenByClient = "/default"
const UploadedFieldFieldForMultipart = "uploaded_file"

const ReplicationQueueSize = 10000
const ReplicationBackoffBaseDelay = 1
const ReplicationBackoffMaxDelay = 60
const ReplicationHttpTimeout = 60
const ReplicationMissingFilesPath = "/_missing"
const ReplicationMissingFilesBatchSize = 256*1024 // bytes, but only approximate