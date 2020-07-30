#include "poller.h"
#include <assert.h>

extern const struct poll_impl basic_poller_impl;

struct poller *poller_create(void) {
	// TODO: Other poll impls
	return basic_poller_impl.create();
}

int poller_destroy(struct poller *poller) {
	assert(poller);
	assert(poller->impl);
	return poller->impl->destroy(poller);
}

struct event_source_fd *poller_add_fd(struct poller *poller, int fd, uint32_t mask,
				      event_source_fd_func_t func, void *data) {
	assert(poller);
	assert(poller->impl);
	return poller->impl->add_fd(poller, fd, mask, func, data);
}

int event_source_fd_destroy(struct event_source_fd *event_source) {
	assert(event_source);
	assert(event_source->impl);
	return event_source->impl->destroy(event_source);
}

struct event_source_signal *poller_add_signal(struct poller *poller, int signal,
					      event_source_signal_func_t func, void *data) {
	assert(poller);
	assert(poller->impl);
	return poller->impl->add_signal(poller, signal, func, data);
}

int event_source_signal_destroy(struct event_source_signal *event_source) {
	assert(event_source);
	assert(event_source->impl);
	return event_source->impl->destroy(event_source);
}

int event_source_fd_update(struct event_source_fd *event_source, uint32_t mask) {
	assert(event_source);
	assert(event_source->impl);
	return event_source->impl->update(event_source, mask);
}

int poller_poll(struct poller *poller) {
	assert(poller);
	assert(poller->impl);
	return poller->impl->poll(poller);
}
