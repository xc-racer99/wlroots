#ifndef BACKEND_FBDEV_H
#define BACKEND_FBDEV_H

#include <wlr/backend/fbdev.h>
#include <wlr/backend/interface.h>

#define FBDEV_DEFAULT_REFRESH (60 * 1000) // 60 Hz

struct wlr_fbdev_backend {
	struct wlr_backend backend;
	struct wlr_egl egl;
	struct wlr_renderer *renderer;
	struct wl_display *display;
	struct wl_list outputs;
	size_t last_output_num;
	struct wl_list input_devices;
	struct wl_listener display_destroy;
	bool started;
};

struct wlr_fbdev_output {
	struct wlr_output wlr_output;

	struct wlr_fbdev_backend *backend;
	struct wl_list link;

	void *egl_surface;
	struct wl_event_source *frame_timer;
	int frame_delay; // ms
};

struct wlr_fbdev_input_device {
	struct wlr_input_device wlr_input_device;

	struct wlr_fbdev_backend *backend;
};

struct wlr_fbdev_backend *fbdev_backend_from_backend(
	struct wlr_backend *wlr_backend);

#endif
