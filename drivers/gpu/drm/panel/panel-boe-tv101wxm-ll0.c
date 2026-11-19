// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Alexander Warnecke <awarnecke002@hotmail.com>
 * Copyright (c) 2023 Manuel Traut <manut@mecka.net>
 * Copyright (c) 2023 Dang Huynh <danct12@riseup.net>
 * Copyright (c) 2026 Anaël
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_connector.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct boe_tv101wxm_ll0;

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned long mode_flags;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct boe_tv101wxm_ll0 *ctx);
	unsigned int lanes;
	bool lp11_before_reset;
	unsigned int vcioo_to_lp11_delay_ms;
	unsigned int lp11_to_reset_delay_ms;
	unsigned int backlight_off_to_display_off_delay_ms;
	unsigned int enter_sleep_to_reset_down_delay_ms;
	unsigned int power_off_delay_ms;
};

struct boe_tv101wxm_ll0 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct panel_desc *desc;
	struct regulator *power;
	struct gpio_desc *enable;
	struct gpio_desc *reset;
	enum drm_panel_orientation orientation;
};

static void boe_tv101wxm_ll0_reset(struct boe_tv101wxm_ll0 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset, 0);
	mdelay(1);
	gpiod_set_value_cansleep(ctx->reset, 1);
	mdelay(6);
	gpiod_set_value_cansleep(ctx->reset, 0);
	msleep(150);
}

static int boe_tv101wxm_ll0_enable(struct boe_tv101wxm_ll0 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int boe_tv101wxm_ll0_disable(struct drm_panel *panel)
{
	struct boe_tv101wxm_ll0 *ctx = container_of(panel, struct boe_tv101wxm_ll0, panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	if (ctx->desc->backlight_off_to_display_off_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->backlight_off_to_display_off_delay_ms);

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);

	if (ctx->desc->enter_sleep_to_reset_down_delay_ms)
		mipi_dsi_msleep(&dsi_ctx, ctx->desc->enter_sleep_to_reset_down_delay_ms);

	return dsi_ctx.accum_err;
}

static int boe_tv101wxm_ll0_unprepare(struct drm_panel *panel)
{
	struct boe_tv101wxm_ll0 *ctx = container_of(panel, struct boe_tv101wxm_ll0, panel);

	gpiod_set_value_cansleep(ctx->reset, 0);

	if (ctx->desc->power_off_delay_ms)
		msleep(ctx->desc->power_off_delay_ms);

	return 0;
}

static int boe_tv101wxm_ll0_prepare(struct drm_panel *panel)
{
	struct boe_tv101wxm_ll0 *ctx = container_of(panel, struct boe_tv101wxm_ll0, panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->desc->vcioo_to_lp11_delay_ms)
		msleep(ctx->desc->vcioo_to_lp11_delay_ms);

	msleep(50);
	boe_tv101wxm_ll0_reset(ctx);

	ret = ctx->desc->init(ctx);
	if (ret)
		return ret;

	return 0;
}

static const struct drm_display_mode boe_tv101wxm_ll0_default_mode = {
	.clock        = 72000,
	.hdisplay     = 1280,
	.hsync_start  = 1280 + 40,
	.hsync_end    = 1280 + 40 + 40,
	.htotal       = 1280 + 40 + 40 + 40,
	.vdisplay     = 800,
	.vsync_start  = 800 + 10,
	.vsync_end    = 800 + 10 + 10,
	.vtotal       = 800 + 10 + 10 + 10,
	.width_mm     = 217,
	.height_mm    = 136,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct panel_desc boe_tv101wxm_ll0_desc = {
	.modes = &boe_tv101wxm_ll0_default_mode,
	.lanes = 4,
	.format = MIPI_DSI_FMT_RGB888,
	.mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE,
	.init = boe_tv101wxm_ll0_enable,
};

static int boe_tv101wxm_ll0_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	struct boe_tv101wxm_ll0 *ctx = container_of(panel, struct boe_tv101wxm_ll0, panel);
	const struct drm_display_mode *desc_mode = ctx->desc->modes;

	connector->display_info.bpc = 8;
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	return drm_connector_helper_get_modes_fixed(connector, desc_mode);
}

static enum drm_panel_orientation boe_tv101wxm_ll0_get_orientation(struct drm_panel *panel)
{
	struct boe_tv101wxm_ll0 *ctx = container_of(panel, struct boe_tv101wxm_ll0, panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs boe_tv101wxm_ll0_funcs = {
	.prepare = boe_tv101wxm_ll0_prepare,
	.unprepare = boe_tv101wxm_ll0_unprepare,
	.disable = boe_tv101wxm_ll0_disable,
	.get_modes = boe_tv101wxm_ll0_get_modes,
	.get_orientation = boe_tv101wxm_ll0_get_orientation,
};

static int boe_tv101wxm_ll0_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct boe_tv101wxm_ll0 *ctx;
	const struct panel_desc *desc;
	int ret;

	ctx = devm_drm_panel_alloc(&dsi->dev, struct boe_tv101wxm_ll0, panel,
				   &boe_tv101wxm_ll0_funcs, DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dsi = dsi;

	desc = of_device_get_match_data(&dsi->dev);
	dsi->lanes = desc->lanes;
	dsi->format = desc->format;
	dsi->mode_flags = desc->mode_flags;
	ctx->desc = desc;

	ctx->reset = devm_gpiod_get_optional(&dsi->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset))
		return dev_err_probe(&dsi->dev, PTR_ERR(ctx->reset),
				     "Failed to get reset GPIO\n");

	ret = of_drm_get_panel_orientation(dsi->dev.of_node, &ctx->orientation);
	if (ret)
		return dev_err_probe(&dsi->dev, ret, "Failed to get orientation\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		dev_err_probe(&dsi->dev, ret, "Failed to attach panel to DSI host\n");
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	return 0;
}

static void boe_tv101wxm_ll0_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct boe_tv101wxm_ll0 *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id boe_tv101wxm_ll0_of_match[] = {
	{
		.compatible = "boe,tv101wxm-ll0",
		.data = &boe_tv101wxm_ll0_desc
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, boe_tv101wxm_ll0_of_match);

static struct mipi_dsi_driver boe_tv101wxm_ll0_driver = {
	.driver = {
		.name = "boe-tv101wxm-ll0",
		.of_match_table = boe_tv101wxm_ll0_of_match,
	},
	.probe = boe_tv101wxm_ll0_dsi_probe,
	.remove = boe_tv101wxm_ll0_dsi_remove,
};
module_mipi_dsi_driver(boe_tv101wxm_ll0_driver);

MODULE_AUTHOR("Anaël");
MODULE_DESCRIPTION("BOE TV101WXM-LL0 MIPI-DSI LCD panel driver");
MODULE_LICENSE("GPL");
