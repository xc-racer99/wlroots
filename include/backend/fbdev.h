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
	struct wl_listener display_destroy;
	struct udev *udev;
	bool started;
};

struct wlr_fbdev_output {
	struct wlr_output wlr_output;

	struct wlr_fbdev_backend *backend;
	struct wl_list link;

	struct wl_event_source *frame_timer;
	int refresh; // Hz
	int width;
	int height;
};

struct wlr_fbdev_backend *fbdev_backend_from_backend(
	struct wlr_backend *wlr_backend);

#endif
