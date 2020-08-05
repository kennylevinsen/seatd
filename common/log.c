#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

const long NSEC_PER_SEC = 1000000000;

static enum log_level current_log_level;
static struct timespec start_time = {-1, -1};
static bool colored = false;

static const char *verbosity_colors[] = {
	[LOGLEVEL_SILENT] = "",
	[LOGLEVEL_ERROR] = "\x1B[1;31m",
	[LOGLEVEL_INFO] = "\x1B[1;34m",
	[LOGLEVEL_DEBUG] = "\x1B[1;90m",
};

static const char *verbosity_headers[] = {
	[LOGLEVEL_SILENT] = "",
	[LOGLEVEL_ERROR] = "[ERROR]",
	[LOGLEVEL_INFO] = "[INFO]",
	[LOGLEVEL_DEBUG] = "[DEBUG]",
};

static void timespec_sub(struct timespec *r, const struct timespec *a, const struct timespec *b) {
	r->tv_sec = a->tv_sec - b->tv_sec;
	r->tv_nsec = a->tv_nsec - b->tv_nsec;
	if (r->tv_nsec < 0) {
		r->tv_sec--;
		r->tv_nsec += NSEC_PER_SEC;
	}
}

void log_init(enum log_level level) {
	if (start_time.tv_sec >= 0) {
		return;
	}
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	current_log_level = level;
	colored = isatty(STDERR_FILENO);
}

void _logf(enum log_level level, const char *fmt, ...) {
	int stored_errno = errno;
	va_list args;
	if (level > current_log_level) {
		return;
	}

	struct timespec ts = {0};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	timespec_sub(&ts, &ts, &start_time);
	unsigned c = (level < LOGLEVEL_LAST) ? level : LOGLEVEL_LAST - 1;

	const char *prefix, *postfix;

	if (colored) {
		prefix = verbosity_colors[c];
		postfix = "\x1B[0m\n";
	} else {
		prefix = verbosity_headers[c];
		postfix = "\n";
	}

	fprintf(stderr, "%02d:%02d:%02d.%03ld %s ", (int)(ts.tv_sec / 60 / 60),
		(int)(ts.tv_sec / 60 % 60), (int)(ts.tv_sec % 60), ts.tv_nsec / 1000000, prefix);

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);

	fprintf(stderr, "%s", postfix);
	errno = stored_errno;
}
