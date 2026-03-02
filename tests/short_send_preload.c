#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef ssize_t (*send_func_t)(int, const void *, size_t, int);

static send_func_t real_send_func;
static const char *match_prefix;
static size_t match_prefix_len;
static int short_send_armed = 1;

static void
short_send_init (void)
{
	if (real_send_func)
		return;

	real_send_func = (send_func_t) dlsym (RTLD_NEXT, "send");
	match_prefix = getenv ("HEXCHAT_SHORT_SEND_PREFIX");
	if (match_prefix)
		match_prefix_len = strlen (match_prefix);
}

ssize_t
send (int sockfd, const void *buf, size_t len, int flags)
{
	size_t forced_len;

	short_send_init ();
	if (!real_send_func)
	{
		errno = EIO;
		return -1;
	}

	if (!short_send_armed || !match_prefix || match_prefix_len == 0)
		return real_send_func (sockfd, buf, len, flags);

	if (len <= match_prefix_len || memcmp (buf, match_prefix, match_prefix_len) != 0)
		return real_send_func (sockfd, buf, len, flags);

	forced_len = len / 2;
	if (forced_len == 0)
		forced_len = 1;
	if (forced_len >= len)
		forced_len = len - 1;

	short_send_armed = 0;
	(void) write (STDERR_FILENO, "HEXCHAT_SHORT_SEND_TRIGGERED\n", 29);

	return real_send_func (sockfd, buf, forced_len, flags);
}
