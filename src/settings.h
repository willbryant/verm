#define DEFAULT_HTTP_PORT "1138"

#define MAX_DIRECTORY_LENGTH 256
#define MAX_PATH_LENGTH 512 // checked; enough for /data root directory:200/client-requested directory:256/hashed filename:44.extension:8
#undef  DEBUG

#ifdef DEBUG
	#define DEBUG_PRINT(...) fprintf(stdout, __VA_ARGS__)
#else
	#define DEBUG_PRINT(...) 
#endif
