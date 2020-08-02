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

/* Used for signal handling */
struct poller *global_poller = NULL;

void poller_init(struct poller *poller) {
	assert(global_poller == NULL);

	list_init(&poller->fds);
	list_init(&poller->new_fds);
	list_init(&poller->signals);
	list_init(&poller->new_signals);
	global_poller = poller;
}

int poller_finish(struct poller *poller) {
	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		struct event_source_fd *bpfd = poller->fds.items[idx];
		free(bpfd);
	}
	list_free(&poller->fds);
	for (size_t idx = 0; idx < poller->new_fds.length; idx++) {
		struct event_source_fd *bpfd = poller->new_fds.items[idx];
		free(bpfd);
	}
	list_free(&poller->new_fds);
	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct event_source_signal *bps = poller->signals.items[idx];

		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(bps->signal, &sa, NULL);

		free(bps);
	}
	list_free(&poller->signals);
	for (size_t idx = 0; idx < poller->new_signals.length; idx++) {
		struct event_source_signal *bps = poller->new_signals.items[idx];

		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(bps->signal, &sa, NULL);

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

static int regenerate_pollfds(struct poller *poller) {
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
		struct event_source_fd *bpfd = poller->fds.items[idx];
		poller->pollfds[idx] = (struct pollfd){
			.fd = bpfd->fd,
			.events = event_mask_to_poll_mask(bpfd->mask),
		};
	}

	return 0;
}

struct event_source_fd *poller_add_fd(struct poller *poller, int fd, uint32_t mask,
				      event_source_fd_func_t func, void *data) {
	struct event_source_fd *bpfd = calloc(1, sizeof(struct event_source_fd));
	if (bpfd == NULL) {
		return NULL;
	}
	bpfd->fd = fd;
	bpfd->mask = mask;
	bpfd->data = data;
	bpfd->func = func;
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

int event_source_fd_destroy(struct event_source_fd *event_source) {
	struct event_source_fd *bpfd = (struct event_source_fd *)event_source;
	struct poller *poller = bpfd->poller;
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

int event_source_fd_update(struct event_source_fd *event_source, uint32_t mask) {
	struct event_source_fd *bpfd = (struct event_source_fd *)event_source;
	struct poller *poller = bpfd->poller;
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
		struct event_source_signal *bps = global_poller->signals.items[idx];
		if (bps->signal == sig) {
			bps->raised = true;
		}
	}
}

struct event_source_signal *poller_add_signal(struct poller *poller, int signal,
					      event_source_signal_func_t func, void *data) {

	struct event_source_signal *bps = calloc(1, sizeof(struct event_source_signal));
	if (bps == NULL) {
		return NULL;
	}

	int refcnt = 0;
	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct event_source_signal *bps = poller->signals.items[idx];
		if (bps->signal == signal) {
			refcnt++;
		}
	}

	bps->signal = signal;
	bps->data = data;
	bps->func = func;
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

int event_source_signal_destroy(struct event_source_signal *event_source) {
	struct event_source_signal *bps = (struct event_source_signal *)event_source;
	struct poller *poller = bps->poller;

	int idx = list_find(&poller->signals, event_source);
	if (idx == -1) {
		return -1;
	}

	int refcnt = 0;
	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct event_source_signal *b = poller->signals.items[idx];
		if (b->signal == bps->signal) {
			refcnt++;
		}
	}

	if (refcnt == 0) {
		struct sigaction sa;
		sa.sa_handler = SIG_DFL;
		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sigaction(bps->signal, &sa, NULL);
	}

	if (poller->inpoll) {
		bps->killed = true;
	} else {
		list_del(&poller->signals, idx);
		free(bps);
	}
	return 0;
}

int poller_poll(struct poller *poller) {
	if (poll(poller->pollfds, poller->fds.length, -1) == -1 && errno != EINTR) {
		return -1;
	}

	poller->inpoll = true;

	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		short revents = poller->pollfds[idx].revents;
		if (revents == 0) {
			continue;
		}
		struct event_source_fd *bpfd = poller->fds.items[idx];
		bpfd->func(poller->pollfds[idx].fd, poll_mask_to_event_mask(revents), bpfd->data);
	}

	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct event_source_signal *bps = poller->signals.items[idx];
		if (!bps->raised) {
			continue;
		}
		bps->func(bps->signal, bps->data);
		bps->raised = false;
	}

	poller->inpoll = false;

	for (size_t idx = 0; idx < poller->fds.length; idx++) {
		struct event_source_fd *bpfd = poller->fds.items[idx];
		if (!bpfd->killed) {
			continue;
		}

		list_del(&poller->fds, idx);
		free(bpfd);
		idx--;
	}

	for (size_t idx = 0; idx < poller->signals.length; idx++) {
		struct event_source_signal *bps = poller->signals.items[idx];
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
