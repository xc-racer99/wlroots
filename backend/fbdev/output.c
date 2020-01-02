#include <assert.h>
#include <fcntl.h>
#include <libudev.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "backend/fbdev.h"
#include "util/signal.h"

static struct wlr_fbdev_output *fbdev_output_from_output(
		struct wlr_output *wlr_output) {
	assert(wlr_output_is_fbdev(wlr_output));
	return (struct wlr_fbdev_output *)wlr_output;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, int32_t width,
		int32_t height, int32_t refresh) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);

	/* Always return our static setup */
	wlr_output_update_custom_mode(&output->wlr_output, output->width,
		output->height, output->refresh);

	return true;
}

static bool output_attach_render(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);
	bool ret;
	ret = wlr_egl_make_current(&output->backend->egl,
		&output->backend->egl.surface, buffer_age);

	if (ret == false) {
		wlr_log(WLR_ERROR, "EGL surface handle is %p, context %p", output->backend->egl.surface, output->backend->egl.context);
		wlr_log(WLR_ERROR, "Failed to make current when attaching output: %d", eglGetError());
	}

	return ret;
}

static bool output_commit(struct wlr_output *wlr_output) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);

#if 0
	pixman_region32_t *damage = NULL;
	if (wlr_output->pending.committed & WLR_OUTPUT_STATE_DAMAGE) {
		damage = &wlr_output->pending.damage;
	}

	if (!wlr_egl_swap_buffers(&output->backend->egl, &output->backend->egl.surface, damage)) {
		return false;
	}
#else
	if (!wlr_egl_swap_buffers(&output->backend->egl,
			&output->backend->egl.surface, NULL)) {
		return false;
	}
#endif

	wlr_output_send_present(wlr_output, NULL);
	return true;
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_fbdev_output *output =
		fbdev_output_from_output(wlr_output);

	wl_list_remove(&output->link);

	wl_event_source_remove(output->frame_timer);

	wlr_egl_destroy_surface(&output->backend->egl, &output->backend->egl.surface);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.destroy = output_destroy,
	.attach_render = output_attach_render,
	.commit = output_commit,
};

bool wlr_output_is_fbdev(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static int signal_frame(void *data) {
	struct wlr_fbdev_output *output = data;
	wlr_output_send_frame(&output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, 1000000 / output->refresh);
	return 0;
}

static int calculate_refresh_rate(struct fb_var_screeninfo *vinfo)
{
	uint64_t quot;

	/* Calculate monitor refresh rate. Default is 60 Hz. Units are Hz. */
	quot = (vinfo->upper_margin + vinfo->lower_margin + vinfo->yres);
	quot *= (vinfo->left_margin + vinfo->right_margin + vinfo->xres);
	quot *= vinfo->pixclock;

	if (quot > 0) {
		uint64_t refresh_rate;

		refresh_rate = 1000000000000LLU / quot;
		if (refresh_rate > 200)
			refresh_rate = 200; /* cap at 200 Hz */

		if (refresh_rate >= 1) /* at least 1 Hz */
			return refresh_rate;
	}

	return 60; /* default to 60 Hz */
}

static bool determine_fb_info(struct wlr_fbdev_output *output, const char *path) {
	struct fb_var_screeninfo varinfo;
	int fd;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to open %s", path);
		return false;
	}

	/* Probe the device for screen information. */
	if (ioctl(fd, FBIOGET_VSCREENINFO, &varinfo) < 0) {
		wlr_log(WLR_ERROR, "FB ioctl failed");
		close(fd);
		return false;
	}

	close(fd);

	output->width = varinfo.xres;
	output->height = varinfo.yres;

	// TODO determine/set pixelformat to something 8-8-8 (eg RGB888)

	output->refresh = calculate_refresh_rate(&varinfo);

	wlr_log(WLR_INFO, "width %d height %d refresh rate %d", output->width, output->height, output->refresh);

	return true;
}

static void fbdev_add_output(struct wlr_backend *wlr_backend, const char *path) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);

	struct wlr_fbdev_output *output =
		calloc(1, sizeof(struct wlr_fbdev_output));
	if (output == NULL) {
		wlr_log(WLR_ERROR, "Failed to allocate wlr_fbdev_output");
		return;
	}
	output->backend = backend;
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	if (!determine_fb_info(output, path)) {
		wlr_log(WLR_ERROR, "Failed to determine FB info");
		goto error;
	}

	output_set_custom_mode(wlr_output, output->width, output->height, 0);
	strncpy(wlr_output->make, "fbdev", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "fbdev", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "FBDEV-%zd",
		++backend->last_output_num);

	if (!wlr_egl_make_current(&backend->egl, backend->egl.surface,
			NULL)) {
		wlr_log(WLR_ERROR, "Failed to make EGL context current");
		goto error;
	}

	wlr_renderer_begin(backend->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&backend->outputs, &output->link);

	if (backend->started) {
		wl_event_source_timer_update(output->frame_timer, 1000000 / output->refresh);
		wlr_output_update_enabled(wlr_output, true);
		wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	}

	return;
error:
	wlr_output_destroy(&output->wlr_output);
}

void wlr_fbdev_add_outputs(struct wlr_backend *wlr_backend) {
	struct wlr_fbdev_backend *backend =
		fbdev_backend_from_backend(wlr_backend);
	struct udev_enumerate *udev_enum = udev_enumerate_new(backend->udev);
	struct udev_list_entry *devices, *entry;
	struct udev_device *device;
	const char *dev_path, *path;

	udev_enumerate_add_match_subsystem(udev_enum, "graphics");
	udev_enumerate_add_match_sysname(udev_enum, "fb[0-9]*");

	udev_enumerate_scan_devices(udev_enum);

	devices = udev_enumerate_get_list_entry(udev_enum);

	udev_list_entry_foreach(entry, devices) {
		path = udev_list_entry_get_name(entry);
		if (!path) {
			wlr_log(WLR_INFO, "Found FB entry without path");
			continue;
		}

		device = udev_device_new_from_syspath(backend->udev, path);
		if (!path) {
			wlr_log(WLR_INFO, "Fail making device from %s", path);
			continue;
		}

		dev_path = udev_device_get_devnode(device);
		if (!dev_path) {
			wlr_log(WLR_INFO, "Fail getting dev path from %s", path);
			continue;
		}

		wlr_log(WLR_INFO, "Found FB %s", dev_path);
		fbdev_add_output(wlr_backend, dev_path);

		udev_device_unref(device);

		/* Only allow adding 1 display */
		break;
	}

	wlr_log(WLR_INFO, "Finished adding FBs");

	udev_enumerate_unref(udev_enum);
}
