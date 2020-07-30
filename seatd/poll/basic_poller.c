#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"
#include "poller.h"

struct basic_poller *global_poller = NULL;

const struct poll_impl basic_poller_impl;
const struct event_source_fd_impl basic_poller_fd_impl;
const struct event_source_signal_impl basic_poller_signal_impl;

struct basic_poller {
	struct poller base;
	struct list signals;
	struct list new_signals;
	struct list fds;
	struct list new_fds;

	struct pollfd *pollfds;
	size_t pollfds_len;
	bool dirty;
	bool inpoll;
};

struct basic_poller_fd {
	struct event_source_fd base;
	struct basic_poller *poller;
	bool killed;
};

struct basic_poller_signal {
	struct event_source_signal base;
	struct basic_poller *poller;
	bool raised;
	bool killed;
};

static struct basic_poller *basic_poller_from_poller(struct poller *base) {
	assert(base->impl == &basic_poller_impl);
	return (struct basic_poller *)base;
}

static struct poller *basic_poller_create(void) {
	if (global_poller != NULL) {
		errno = EEXIST;
		return NULL;
	}

	struct basic_poller *poller = calloc(1, sizeof(struct basic_poller));
	if (poller == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	list_init(&poller->fds);
	list_init(&poller->new_fds);
	list_init(&poller->signals);
	list_init(&poller->new_signals);
	poller->base.impl = &basic_poller_impl;
	global_poller = poller;
	return (struct poller *)poller;
}

static int destroy(struct poller *base) {
	struct basic_poller *poller = basic_poller_from_poller(base);
	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		struct basic_poller_fd *bpfd = poller->fds.items[idx];
		free(bpfd);
	}
	list_free(&poller->fds);
	for (size_t idx = 0; idx < poller->new_fds.length; idx++) {
		struct basic_poller_fd *bpfd = poller->new_fds.items[idx];
		free(bpfd);
	}
	list_free(&poller->new_fds);
	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct basic_poller_signal *bps = poller->signals.items[idx];

		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(bps->base.signal, &sa, NULL);

		free(bps);
	}
	list_free(&poller->signals);
	for (size_t idx = 0; idx < poller->new_signals.length; idx++) {
		struct basic_poller_signal *bps = poller->new_signals.items[idx];

		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(bps->base.signal, &sa, NULL);

		free(bps);
	}
	list_free(&poller->new_signals);
	free(poller->pollfds);
	return 0;
}

static int event_mask_to_poll_mask(uint32_t event_mask) {
	int poll_mask = 0;
	if (event_mask & EVENT_READABLE) {
		poll_mask |= POLLIN;
	}
	if (event_mask & EVENT_WRITABLE) {
		poll_mask |= POLLOUT;
	}
	return poll_mask;
}

static uint32_t poll_mask_to_event_mask(int poll_mask) {
	uint32_t event_mask = 0;
	if (poll_mask & POLLIN) {
		event_mask |= EVENT_READABLE;
	}
	if (poll_mask & POLLOUT) {
		event_mask |= EVENT_WRITABLE;
	}
	if (poll_mask & POLLERR) {
		event_mask |= EVENT_ERROR;
	}
	if (poll_mask & POLLHUP) {
		event_mask |= EVENT_HANGUP;
	}
	return event_mask;
}

static int regenerate_pollfds(struct basic_poller *poller) {
	if (poller->pollfds_len != poller->fds.length) {
		struct pollfd *fds = calloc(poller->fds.length, sizeof(struct pollfd));
		if (fds == NULL) {
			return -1;
		}
		free(poller->pollfds);
		poller->pollfds = fds;
		poller->pollfds_len = poller->fds.length;
	}

	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		struct basic_poller_fd *bpfd = poller->fds.items[idx];
		poller->pollfds[idx] = (struct pollfd){
			.fd = bpfd->base.fd,
			.events = event_mask_to_poll_mask(bpfd->base.mask),
		};
	}

	return 0;
}

static struct event_source_fd *add_fd(struct poller *base, int fd, uint32_t mask,
				      event_source_fd_func_t func, void *data) {
	struct basic_poller *poller = basic_poller_from_poller(base);

	struct basic_poller_fd *bpfd = calloc(1, sizeof(struct basic_poller_fd));
	if (bpfd == NULL) {
		return NULL;
	}
	bpfd->base.impl = &basic_poller_fd_impl;
	bpfd->base.fd = fd;
	bpfd->base.mask = mask;
	bpfd->base.data = data;
	bpfd->base.func = func;
	bpfd->poller = poller;
	poller->dirty = true;
	if (poller->inpoll) {
		list_add(&poller->new_fds, bpfd);
	} else {
		list_add(&poller->fds, bpfd);
		regenerate_pollfds(poller);
	}
	return (struct event_source_fd *)bpfd;
}

static int fd_destroy(struct event_source_fd *event_source) {
	struct basic_poller_fd *bpfd = (struct basic_poller_fd *)event_source;
	struct basic_poller *poller = bpfd->poller;
	int idx = list_find(&poller->fds, event_source);
	if (idx == -1) {
		return -1;
	}
	poller->dirty = true;
	if (poller->inpoll) {
		bpfd->killed = true;
	} else {
		list_del(&poller->fds, idx);
		free(bpfd);
		regenerate_pollfds(poller);
	}
	return 0;
}

static int fd_update(struct event_source_fd *event_source, uint32_t mask) {
	struct basic_poller_fd *bpfd = (struct basic_poller_fd *)event_source;
	struct basic_poller *poller = bpfd->poller;
	event_source->mask = mask;

	poller->dirty = true;
	if (!poller->inpoll) {
		regenerate_pollfds(poller);
	}
	return 0;
}

static void signal_handler(int sig) {
	if (global_poller == NULL) {
		return;
	}

	for (size_t idx = 0; idx < global_poller->signals.length; idx++) {
		struct basic_poller_signal *bps = global_poller->signals.items[idx];
		if (bps->base.signal == sig) {
			bps->raised = true;
		}
	}
}

static struct event_source_signal *add_signal(struct poller *base, int signal,
					      event_source_signal_func_t func, void *data) {
	struct basic_poller *poller = basic_poller_from_poller(base);

	struct basic_poller_signal *bps = calloc(1, sizeof(struct basic_poller_signal));
	if (bps == NULL) {
		return NULL;
	}

	int refcnt = 0;
	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct basic_poller_signal *bps = poller->signals.items[idx];
		if (bps->base.signal == signal) {
			refcnt++;
		}
	}

	bps->base.impl = &basic_poller_signal_impl;
	bps->base.signal = signal;
	bps->base.data = data;
	bps->base.func = func;
	bps->poller = poller;

	if (refcnt == 0) {
		struct sigaction sa;
		sa.sa_handler = &signal_handler;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(signal, &sa, NULL);
	}

	if (poller->inpoll) {
		list_add(&poller->new_signals, bps);
	} else {
		list_add(&poller->signals, bps);
	}

	return (struct event_source_signal *)bps;
}

static int signal_destroy(struct event_source_signal *event_source) {
	struct basic_poller_signal *bps = (struct basic_poller_signal *)event_source;
	struct basic_poller *poller = bps->poller;

	int idx = list_find(&poller->signals, event_source);
	if (idx == -1) {
		return -1;
	}

	int refcnt = 0;
	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct basic_poller_signal *b = poller->signals.items[idx];
		if (b->base.signal == bps->base.signal) {
			refcnt++;
		}
	}

	if (refcnt == 0) {
		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(bps->base.signal, &sa, NULL);
	}

	if (poller->inpoll) {
		bps->killed = true;
	} else {
		list_del(&poller->signals, idx);
		free(bps);
	}
	return 0;
}

static int basic_poller_poll(struct poller *base) {
	struct basic_poller *poller = basic_poller_from_poller(base);

	if (poll(poller->pollfds, poller->fds.length, -1) == -1 && errno != EINTR) {
		return -1;
	}

	poller->inpoll = true;

	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		short revents = poller->pollfds[idx].revents;
		if (revents == 0) {
			continue;
		}
		struct basic_poller_fd *bpfd = poller->fds.items[idx];
		bpfd->base.func(poller->pollfds[idx].fd, poll_mask_to_event_mask(revents),
				bpfd->base.data);
	}

	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct basic_poller_signal *bps = poller->signals.items[idx];
		if (!bps->raised) {
			continue;
		}
		bps->base.func(bps->base.signal, bps->base.data);
		bps->raised = false;
	}

	poller->inpoll = false;

	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		struct basic_poller_fd *bpfd = poller->fds.items[idx];
		if (!bpfd->killed) {
			continue;
		}

		list_del(&poller->fds, idx);
		free(bpfd);
		idx--;
	}

	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct basic_poller_signal *bps = poller->signals.items[idx];
		if (!bps->killed) {
			continue;
		}

		list_del(&poller->signals, idx);
		free(bps);
		idx--;
	}

	if (poller->new_fds.length > 0) {
		list_concat(&poller->fds, &poller->new_fds);
		list_truncate(&poller->new_fds);
	}

	if (poller->new_signals.length > 0) {
		list_concat(&poller->signals, &poller->new_signals);
		list_truncate(&poller->new_signals);
	}

	if (poller->dirty) {
		regenerate_pollfds(poller);
		poller->dirty = false;
	}

	return 0;
}

const struct event_source_fd_impl basic_poller_fd_impl = {
	.update = fd_update,
	.destroy = fd_destroy,
};

const struct event_source_signal_impl basic_poller_signal_impl = {
	.destroy = signal_destroy,
};

const struct poll_impl basic_poller_impl = {
	.create = basic_poller_create,
	.destroy = destroy,
	.add_fd = add_fd,
	.add_signal = add_signal,
	.poll = basic_poller_poll,
};
