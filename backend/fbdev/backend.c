#include <assert.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
#include "backend/fbdev.h"
#include "glapi.h"
#include "util/signal.h"

struct wlr_fbdev_backend *fbdev_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_fbdev(wlr_backend));
	return (struct wlr_fbdev_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	wlr_log(WLR_INFO, "Starting fbdev backend");

	struct wlr_fbdev_output *output;
	wl_list_for_each(output, &backend->outputs, link) {
		wl_event_source_timer_update(output->frame_timer, 1000000 / output->refresh);
		wlr_output_update_enabled(&output->wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output,
			&output->wlr_output);
	}

	backend->started = true;
	return true;
}

static void backend_destroy(struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	if (!wlr_backend) {
		return;
	}

	wl_list_remove(&backend->display_destroy.link);

	struct wlr_fbdev_output *output, *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &backend->outputs, link) {
		wlr_output_destroy(&output->wlr_output);
	}

	wlr_signal_emit_safe(&wlr_backend->events.destroy, backend);

	wlr_renderer_destroy(backend->renderer);
	wlr_egl_finish(&backend->egl);
	free(backend);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	return backend->renderer;
}

static const struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
};

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_fbdev_backend *backend =
		wl_container_of(listener, backend, display_destroy);

	udev_unref(backend->udev);

	backend_destroy(&backend->backend);
}

struct wlr_backend *wlr_fbdev_backend_create(struct wl_display *display,
		wlr_renderer_create_func_t create_renderer_func) {
	wlr_log(WLR_INFO, "Creating fbdev backend");

	struct wlr_fbdev_backend *backend =
		calloc(1, sizeof(struct wlr_fbdev_backend));
	if (!backend) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_fbdev_backend");
		return NULL;
	}

	backend->udev = udev_new();
	if (!backend->udev) {
		wlr_log(WLR_ERROR, "Failed to create udev context");
		return NULL;
	}

	wlr_backend_init(&backend->backend, &backend_impl);
	backend->display = display;
	wl_list_init(&backend->outputs);

	static const EGLint config_attribs[] = {
		EGL_BUFFER_SIZE, 32,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE,
	};

	if (!create_renderer_func) {
		create_renderer_func = wlr_renderer_autocreate;
	}

	backend->renderer = create_renderer_func(&backend->egl,
		0, NULL, (EGLint*)config_attribs, 0);
	if (!backend->renderer) {
		wlr_log(WLR_ERROR, "Failed to create renderer");
		free(backend);
		return NULL;
	}

	backend->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &backend->display_destroy);

	wlr_log(WLR_DEBUG, "FBDEV backend created");

	return &backend->backend;
}

bool wlr_backend_is_fbdev(struct wlr_backend *backend) {
	return backend->impl == &backend_impl;
}
