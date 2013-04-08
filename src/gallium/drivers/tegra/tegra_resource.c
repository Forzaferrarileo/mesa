#include <stdio.h>

#include <libdrm/tegra_drm.h>
#include <libdrm/tegra.h>

#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_pack_color.h"
#include "util/u_transfer.h"

#include "tegra_context.h"
#include "tegra_resource.h"
#include "tegra_screen.h"

/*
 * XXX Required to access winsys_handle internals. Should go away in favour
 * of some abstraction to handle handles in a Tegra-specific winsys
 * implementation.
 */
#include "state_tracker/drm_driver.h"

#define ALIGN(v, a) (((v) + (a) - 1) & ~((a) - 1))

static boolean
tegra_resource_get_handle(struct pipe_screen *pscreen,
			  struct pipe_resource *presource,
			  struct winsys_handle *handle)
{
	struct tegra_resource *resource = tegra_resource(presource);
	boolean ret = TRUE;
	int err;

	fprintf(stdout, "> %s(pscreen=%p, presource=%p, handle=%p)\n",
		__func__, pscreen, presource, handle);
	fprintf(stdout, "  handle:\n");
	fprintf(stdout, "    type: %u\n", handle->type);
	fprintf(stdout, "    handle: %u\n", handle->handle);
	fprintf(stdout, "    stride: %u\n", handle->stride);

	if (handle->type == DRM_API_HANDLE_TYPE_SHARED) {
		err = drm_tegra_bo_get_name(resource->bo, &handle->handle);
		if (err < 0) {
			fprintf(stderr, "drm_tegra_bo_get_name() failed: %d\n", err);
			ret = FALSE;
			goto out;
		}
	} else if (handle->type == DRM_API_HANDLE_TYPE_KMS) {
		err = drm_tegra_bo_get_handle(resource->bo, &handle->handle);
		if (err < 0) {
			fprintf(stderr, "drm_tegra_bo_get_handle() failed: %d\n", err);
			ret = FALSE;
			goto out;
		}
	} else {
		fprintf(stdout, "unsupported handle type: %d\n",
			handle->type);
		ret = FALSE;
	}

	if (ret) {
		handle->stride = resource->pitch * resource->bpp;
		fprintf(stdout, "  handle: %u\n", handle->handle);
		fprintf(stdout, "  stride: %u\n", handle->stride);
	}

out:
	fprintf(stdout, "< %s() = %d\n", __func__, ret);
	return ret;
}

static void tegra_resource_destroy(struct pipe_screen *pscreen,
				   struct pipe_resource *presource)
{
	struct tegra_resource *resource = tegra_resource(presource);

	fprintf(stdout, "> %s(pscreen=%p, presource=%p)\n", __func__, pscreen,
		presource);

	free(resource);

	fprintf(stdout, "< %s()\n", __func__);
}

static void *tegra_resource_transfer_map(struct pipe_context *pcontext,
					 struct pipe_resource *presource,
					 unsigned level, unsigned usage,
					 const struct pipe_box *box,
					 struct pipe_transfer **transfer)
{
	void *ret = NULL;
	fprintf(stdout, "> %s(pcontext=%p, presource=%p, level=%u, usage=%u, box=%p, transfer=%p)\n",
		__func__, pcontext, presource, level, usage, box, transfer);
	fprintf(stdout, "< %s() = %p\n", __func__, ret);
	return ret;
}

static void
tegra_resource_transfer_flush_region(struct pipe_context *pcontext,
				     struct pipe_transfer *transfer,
				     const struct pipe_box *box)
{
	fprintf(stdout, "> %s(pcontext=%p, transfer=%p, box=%p)\n", __func__,
		pcontext, transfer, box);
	fprintf(stdout, "< %s()\n", __func__);
}

static void tegra_resource_transfer_unmap(struct pipe_context *pcontext,
					  struct pipe_transfer *transfer)
{
	fprintf(stdout, "> %s(pcontext=%p, transfer=%p)\n", __func__, pcontext,
		transfer);
	fprintf(stdout, "< %s()\n", __func__);
}

static const struct u_resource_vtbl tegra_resource_vtbl = {
	.resource_get_handle = tegra_resource_get_handle,
	.resource_destroy = tegra_resource_destroy,
	.transfer_map = tegra_resource_transfer_map,
	.transfer_flush_region = tegra_resource_transfer_flush_region,
	.transfer_unmap = tegra_resource_transfer_unmap,
	.transfer_inline_write = u_default_transfer_inline_write,
};

static boolean
tegra_screen_can_create_resource(struct pipe_screen *pscreen,
				 const struct pipe_resource *template)
{
	bool ret = TRUE;
	fprintf(stdout, "> %s(pscreen=%p, template=%p)\n", __func__, pscreen,
		template);
	fprintf(stdout, "< %s() = %d\n", __func__, ret);
	return ret;
}

static struct pipe_resource *
tegra_screen_resource_create(struct pipe_screen *pscreen,
			     const struct pipe_resource *template)
{
	struct tegra_screen *screen = tegra_screen(pscreen);
	struct tegra_resource *resource;
	uint32_t flags, size;
	int err;

	fprintf(stdout, "> %s(pscreen=%p, template=%p)\n", __func__, pscreen,
		template);
	fprintf(stdout, "  template:\n");
	fprintf(stdout, "    target: %d\n", template->target);
	fprintf(stdout, "    format: %d\n", template->format);
	fprintf(stdout, "    width: %u\n", template->width0);
	fprintf(stdout, "    height: %u\n", template->height0);
	fprintf(stdout, "    depth: %u\n", template->depth0);
	fprintf(stdout, "    array_size: %u\n", template->array_size);
	fprintf(stdout, "    last_level: %u\n", template->last_level);
	fprintf(stdout, "    nr_samples: %u\n", template->nr_samples);
	fprintf(stdout, "    usage: %u\n", template->usage);
	fprintf(stdout, "    bind: %x\n", template->bind);
	fprintf(stdout, "    flags: %x\n", template->flags);

	resource = calloc(1, sizeof(*resource));
	if (!resource) {
		fprintf(stdout, "< %s() = NULL\n", __func__);
		return NULL;
	}

	resource->base.b = *template;

	pipe_reference_init(&resource->base.b.reference, 1);
	resource->base.vtbl = &tegra_resource_vtbl;
	resource->base.b.screen = pscreen;

	resource->bpp = util_format_get_blocksize(template->format);
	resource->pitch = ALIGN(template->width0, 32);

	flags = DRM_TEGRA_GEM_CREATE_BOTTOM_UP;

	/* use linear layout for staging-textures, otherwise tiled */
	if (template->usage != PIPE_USAGE_STAGING) {
		flags |= DRM_TEGRA_GEM_CREATE_TILED;
		size = resource->pitch * resource->bpp * ALIGN(template->height0, 16);
	} else {
		size = resource->pitch * resource->bpp * template->height0;
	}

	fprintf(stdout, "  bpp:%u pitch:%u size:%u flags:%x\n", resource->bpp,
		resource->pitch, size, flags);

	err = drm_tegra_bo_create(screen->drm, flags, size, &resource->bo);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_bo_create() failed: %d\n", err);
		return NULL;
	}

	fprintf(stdout, "< %s() = %p\n", __func__, &resource->base.b);
	return &resource->base.b;
}

static struct pipe_resource *
tegra_screen_resource_from_handle(struct pipe_screen *pscreen,
				  const struct pipe_resource *template,
				  struct winsys_handle *handle)
{
	struct tegra_screen *screen = tegra_screen(pscreen);
	struct tegra_resource *resource;
	int err;

	fprintf(stdout, "> %s(pscreen=%p, template=%p, handle=%p)\n",
		__func__, pscreen, template, handle);
	fprintf(stdout, "  handle:\n");
	fprintf(stdout, "    type: %u\n", handle->type);
	fprintf(stdout, "    handle: %u\n", handle->handle);
	fprintf(stdout, "    stride: %u\n", handle->stride);

	resource = calloc(1, sizeof(*resource));
	if (!resource) {
		fprintf(stdout, "< %s() = NULL\n", __func__);
		return NULL;
	}

	resource->base.b = *template;

	pipe_reference_init(&resource->base.b.reference, 1);
	resource->base.vtbl = &tegra_resource_vtbl;
	resource->base.b.screen = pscreen;

	err = drm_tegra_bo_open(screen->drm, handle->handle, &resource->bo);
	if (err < 0) {
		fprintf(stderr, "drm_tegra_bo_open() failed: %d\n", err);
		free(resource);
		return NULL;
	}

	resource->bpp = util_format_get_blocksize(template->format);
	resource->pitch = ALIGN(template->width0, 32);

	fprintf(stdout, "< %s() = %p\n", __func__, &resource->base.b);
	return &resource->base.b;
}

void tegra_screen_resource_init(struct pipe_screen *pscreen)
{
	pscreen->can_create_resource = tegra_screen_can_create_resource;
	pscreen->resource_create = tegra_screen_resource_create;
	pscreen->resource_from_handle = tegra_screen_resource_from_handle;
	pscreen->resource_get_handle = u_resource_get_handle_vtbl;
	pscreen->resource_destroy = u_resource_destroy_vtbl;
}

static void tegra_resource_copy_region(struct pipe_context *pcontext,
				       struct pipe_resource *dst,
				       unsigned int dst_level,
				       unsigned int dstx, unsigned dsty,
				       unsigned int dstz,
				       struct pipe_resource *src,
				       unsigned int src_level,
				       const struct pipe_box *box)
{
	fprintf(stdout, "> %s(pcontext=%p, dst=%p, dst_level=%u, dstx=%u, dsty=%u, dstz=%u, src=%p, src_level=%u, box=%p)\n",
		__func__, pcontext, dst, dst_level, dstx, dsty, dstz, src,
		src_level, box);
	fprintf(stdout, "< %s()\n", __func__);
}

static void tegra_blit(struct pipe_context *pcontext,
		       const struct pipe_blit_info *info)
{
	fprintf(stdout, "> %s(pcontext=%p, info=%p)\n", __func__, pcontext,
		info);
	fprintf(stdout, "< %s()\n", __func__);
}

static uint32_t pack_color(enum pipe_format format, const float *rgba)
{
	union util_color uc;
	util_pack_color(rgba, format, &uc);
	return uc.ui;
}

static void tegra_clear(struct pipe_context *pcontext, unsigned int buffers,
			const union pipe_color_union *color, double depth,
			unsigned int stencil)
{
	struct tegra_context *context = tegra_context(pcontext);
	struct pipe_framebuffer_state *fb;

	fprintf(stdout, "> %s(pcontext=%p, buffers=%x, color=%p, depth=%f, stencil=%u)\n",
		__func__, pcontext, buffers, color, depth, stencil);

	fb = &context->framebuffer.base;

	if (buffers & PIPE_CLEAR_COLOR) {
		struct pipe_resource *texture = fb->cbufs[0]->texture;
		struct tegra_channel *gr2d = context->gr2d;
		struct tegra_resource *resource;
		unsigned int width, height;
		struct host1x_pushbuf *pb;
		uint32_t stride, value;
		int err;

		value = pack_color(fb->cbufs[0]->format, color->f);
		resource = tegra_resource(fb->cbufs[0]->texture);
		stride = resource->pitch * resource->bpp;
		width = texture->width0;
		height = texture->height0;

		err = host1x_job_append(gr2d->job, gr2d->cmdbuf, 0, &pb);
		if (err < 0) {
			fprintf(stderr, "host1x_job_append() failed: %d\n", err);
			goto out;
		}

		host1x_pushbuf_push(pb, HOST1X_OPCODE_SETCL(0, 0x51, 0));
		host1x_pushbuf_push(pb, HOST1X_OPCODE_EXTEND(0, 0x01));
		host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x09, 0x09));
		host1x_pushbuf_push(pb, 0x0000003a);
		host1x_pushbuf_push(pb, 0x00000000);
		host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x1e, 0x07));
		host1x_pushbuf_push(pb, 0x00000000);

		if (resource->bpp == 16)
			host1x_pushbuf_push(pb, 0x00010044); /* 16-bit depth */
		else
			host1x_pushbuf_push(pb, 0x00020044); /* 32-bit depth */

		host1x_pushbuf_push(pb, 0x000000cc);

		host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x2b, 0x09));
		host1x_pushbuf_relocate(pb, resource->bo, 0, 0);
		host1x_pushbuf_push(pb, 0xdeadbeef);
		host1x_pushbuf_push(pb, stride);
		host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x35, 1));
		host1x_pushbuf_push(pb, value);
		host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x46, 1));
		host1x_pushbuf_push(pb, 0x00100000);
		host1x_pushbuf_push(pb, HOST1X_OPCODE_MASK(0x38, 0x05));
		host1x_pushbuf_push(pb, height << 16 | width);
		host1x_pushbuf_push(pb, 0x00000000);
		host1x_pushbuf_push(pb, HOST1X_OPCODE_EXTEND(1, 0x01));
		host1x_pushbuf_push(pb, HOST1X_OPCODE_NONINCR(0x00, 1));
		host1x_pushbuf_sync(pb, HOST1X_SYNCPT_COND_OP_DONE);
	}

	if (buffers & PIPE_CLEAR_DEPTH) {
		fprintf(stdout, "TODO: clear depth buffer\n");
	}

	if (buffers & PIPE_CLEAR_STENCIL) {
		fprintf(stdout, "TODO: clear stencil buffer\n");
	}

out:
	fprintf(stdout, "< %s()\n", __func__);
}

void tegra_context_resource_init(struct pipe_context *pcontext)
{
	pcontext->transfer_map = u_transfer_map_vtbl;
	pcontext->transfer_flush_region = u_transfer_flush_region_vtbl;
	pcontext->transfer_unmap = u_transfer_unmap_vtbl;
	pcontext->transfer_inline_write = u_transfer_inline_write_vtbl;

	pcontext->resource_copy_region = tegra_resource_copy_region;
	pcontext->blit = tegra_blit;
	pcontext->clear = tegra_clear;
}
