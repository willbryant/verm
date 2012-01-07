#ifndef SA_LEN_H
#define SA_LEN_H

#ifdef NEED_SA_LEN
	#include <sys/un.h>

	static int __sa_len(sa_family_t af) {
		switch (af) {
			case AF_INET:
				return sizeof(struct sockaddr_in);
			
			case AF_INET6:
				return sizeof(struct sockaddr_in);
			
			case AF_LOCAL:
				return sizeof(struct sockaddr_un);
		}

		return 0;
	}

	#define SA_LEN(x) __sa_len(x->sa_family)
#else
	#define SA_LEN(x) x->sa_len
#endif

#endif
