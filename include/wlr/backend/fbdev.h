/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_FBDEV_H
#define WLR_BACKEND_FBDEV_H

#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>

/**
 * Creates a fbdev backend. A fbdev backend will have one output per
 * framebuffer
 */
struct wlr_backend *wlr_fbdev_backend_create(struct wl_display *display,
	wlr_renderer_create_func_t create_renderer_func);
/**
 * Creates new fbdev outputs backed by a hardware framebuffers. You can
 * read pixels from this framebuffer via wlr_renderer_read_pixels
 */
void wlr_fbdev_add_outputs(struct wlr_backend *backend);

bool wlr_backend_is_fbdev(struct wlr_backend *backend);

bool wlr_output_is_fbdev(struct wlr_output *output);

#endif
