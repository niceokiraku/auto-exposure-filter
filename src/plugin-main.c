/*
Auto Exposure (Adaptive Brightness) filter for OBS Studio
Copyright (C) 2026 nice okiraku

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <graphics/graphics.h>
#include <plugin-support.h>
#include <math.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

#define SETTING_TARGET_BRIGHTNESS "target_brightness"
#define SETTING_STRENGTH "strength"
#define SETTING_SPEED "speed"
#define SETTING_MIN_GAIN "min_gain"
#define SETTING_MAX_GAIN "max_gain"
#define SETTING_COMPARISON_MODE "comparison_mode"
#define SETTING_SPLIT_POSITION "split_position"
#define SETTING_PERCENTILE "percentile"
#define SETTING_AUTO_BYPASS "auto_bypass"
#define SETTING_DARK_FADE_START "dark_fade_start"
#define SETTING_DARK_FADE_END "dark_fade_end"
#define SETTING_BRIGHT_FADE_START "bright_fade_start"
#define SETTING_BRIGHT_FADE_END "bright_fade_end"
#define SETTING_SHOW_INDICATOR "show_indicator"

/* Side length (in pixels) of the small render target used to estimate
 * scene brightness. 32x32 = 1024 samples, enough for a stable percentile. */
#define MEASURE_SIZE 32

struct auto_exposure_data {
	obs_source_t *context;

	gs_effect_t *effect;
	gs_eparam_t *param_image;
	gs_eparam_t *param_gain;
	gs_eparam_t *param_comparison_mode;
	gs_eparam_t *param_split_position;
	gs_eparam_t *param_show_indicator;
	gs_eparam_t *param_indicator_value;

	gs_texrender_t *measure_texrender;
	gs_stagesurf_t *measure_stagesurface;
	bool measure_pending;

	/* user settings */
	float target_brightness;
	float strength;
	float speed;
	float min_gain;
	float max_gain;
	bool comparison_mode;
	float split_position;
	float percentile;
	bool auto_bypass;
	float dark_fade_start;
	float dark_fade_end;
	float bright_fade_start;
	float bright_fade_end;
	bool show_indicator;

	/* running state */
	float smoothed_luma;
	float smoothed_gain;
	float correction_pct; /* 0-1, how much correction is being applied right now */
	uint64_t last_time_ns;
};

static const char *auto_exposure_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("AutoExposure.Name");
}

static void auto_exposure_update(void *data, obs_data_t *settings)
{
	struct auto_exposure_data *filter = data;

	filter->target_brightness = (float)obs_data_get_double(settings, SETTING_TARGET_BRIGHTNESS);
	filter->strength = (float)obs_data_get_double(settings, SETTING_STRENGTH);
	filter->speed = (float)obs_data_get_double(settings, SETTING_SPEED);
	filter->min_gain = (float)obs_data_get_double(settings, SETTING_MIN_GAIN);
	filter->max_gain = (float)obs_data_get_double(settings, SETTING_MAX_GAIN);
	filter->comparison_mode = obs_data_get_bool(settings, SETTING_COMPARISON_MODE);
	filter->split_position = (float)obs_data_get_double(settings, SETTING_SPLIT_POSITION);
	filter->percentile = (float)obs_data_get_double(settings, SETTING_PERCENTILE);
	filter->auto_bypass = obs_data_get_bool(settings, SETTING_AUTO_BYPASS);
	filter->dark_fade_start = (float)obs_data_get_double(settings, SETTING_DARK_FADE_START);
	filter->dark_fade_end = (float)obs_data_get_double(settings, SETTING_DARK_FADE_END);
	filter->bright_fade_start = (float)obs_data_get_double(settings, SETTING_BRIGHT_FADE_START);
	filter->bright_fade_end = (float)obs_data_get_double(settings, SETTING_BRIGHT_FADE_END);
	filter->show_indicator = obs_data_get_bool(settings, SETTING_SHOW_INDICATOR);
}

static void auto_exposure_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, SETTING_TARGET_BRIGHTNESS, 0.35);
	obs_data_set_default_double(settings, SETTING_STRENGTH, 0.70);
	obs_data_set_default_double(settings, SETTING_SPEED, 0.20);
	obs_data_set_default_double(settings, SETTING_MIN_GAIN, 0.60);
	obs_data_set_default_double(settings, SETTING_MAX_GAIN, 2.20);
	obs_data_set_default_bool(settings, SETTING_COMPARISON_MODE, false);
	obs_data_set_default_double(settings, SETTING_SPLIT_POSITION, 0.5);
	obs_data_set_default_double(settings, SETTING_PERCENTILE, 50.0);
	obs_data_set_default_bool(settings, SETTING_AUTO_BYPASS, false);
	obs_data_set_default_double(settings, SETTING_DARK_FADE_START, 0.35);
	obs_data_set_default_double(settings, SETTING_DARK_FADE_END, 0.45);
	obs_data_set_default_double(settings, SETTING_BRIGHT_FADE_START, 0.70);
	obs_data_set_default_double(settings, SETTING_BRIGHT_FADE_END, 0.80);
	obs_data_set_default_bool(settings, SETTING_SHOW_INDICATOR, false);
}

static obs_properties_t *auto_exposure_get_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_properties_add_float_slider(props, SETTING_PERCENTILE, obs_module_text("AutoExposure.Percentile"), 1.0,
					 99.0, 1.0);

	obs_properties_add_float_slider(props, SETTING_TARGET_BRIGHTNESS,
					 obs_module_text("AutoExposure.TargetBrightness"), 0.1, 0.9, 0.01);
	obs_properties_add_float_slider(props, SETTING_STRENGTH, obs_module_text("AutoExposure.Strength"), 0.0, 1.0,
					 0.01);
	obs_properties_add_float_slider(props, SETTING_SPEED, obs_module_text("AutoExposure.Speed"), 0.2, 8.0, 0.1);
	obs_properties_add_float_slider(props, SETTING_MIN_GAIN, obs_module_text("AutoExposure.MinGain"), 0.2, 1.0,
					 0.01);
	obs_properties_add_float_slider(props, SETTING_MAX_GAIN, obs_module_text("AutoExposure.MaxGain"), 1.0, 4.0,
					 0.01);

	obs_properties_add_bool(props, SETTING_AUTO_BYPASS, obs_module_text("AutoExposure.AutoBypass"));
	obs_properties_add_float_slider(props, SETTING_DARK_FADE_START, obs_module_text("AutoExposure.DarkFadeStart"),
					 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_DARK_FADE_END, obs_module_text("AutoExposure.DarkFadeEnd"), 0.0,
					 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_BRIGHT_FADE_START,
					 obs_module_text("AutoExposure.BrightFadeStart"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_BRIGHT_FADE_END, obs_module_text("AutoExposure.BrightFadeEnd"),
					 0.0, 1.0, 0.01);
	obs_properties_add_bool(props, SETTING_SHOW_INDICATOR, obs_module_text("AutoExposure.ShowIndicator"));

	obs_properties_add_bool(props, SETTING_COMPARISON_MODE, obs_module_text("AutoExposure.ComparisonMode"));
	obs_properties_add_float_slider(props, SETTING_SPLIT_POSITION, obs_module_text("AutoExposure.SplitPosition"),
					 0.05, 0.95, 0.01);

	return props;
}

static void *auto_exposure_create(obs_data_t *settings, obs_source_t *context)
{
	struct auto_exposure_data *filter = bzalloc(sizeof(struct auto_exposure_data));
	filter->context = context;

	char *effect_path = obs_module_file("auto_exposure.effect");

	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	if (filter->effect) {
		filter->param_image = gs_effect_get_param_by_name(filter->effect, "image");
		filter->param_gain = gs_effect_get_param_by_name(filter->effect, "gain");
		filter->param_comparison_mode = gs_effect_get_param_by_name(filter->effect, "comparison_mode");
		filter->param_split_position = gs_effect_get_param_by_name(filter->effect, "split_pos");
		filter->param_show_indicator = gs_effect_get_param_by_name(filter->effect, "show_indicator");
		filter->param_indicator_value = gs_effect_get_param_by_name(filter->effect, "indicator_value");
	} else {
		obs_log(LOG_ERROR, "Failed to load auto_exposure.effect from %s", effect_path);
	}

	filter->measure_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->measure_stagesurface = gs_stagesurface_create(MEASURE_SIZE, MEASURE_SIZE, GS_RGBA);
	obs_leave_graphics();

	bfree(effect_path);

	filter->smoothed_gain = 1.0f;
	filter->smoothed_luma = -1.0f; /* sentinel: not yet measured */
	filter->measure_pending = false;
	filter->last_time_ns = obs_get_video_frame_time();

	auto_exposure_update(filter, settings);

	return filter;
}

static void auto_exposure_destroy(void *data)
{
	struct auto_exposure_data *filter = data;

	obs_enter_graphics();
	if (filter->effect)
		gs_effect_destroy(filter->effect);
	if (filter->measure_texrender)
		gs_texrender_destroy(filter->measure_texrender);
	if (filter->measure_stagesurface)
		gs_stagesurface_destroy(filter->measure_stagesurface);
	obs_leave_graphics();

	bfree(filter);
}

/* Reads back the tiny texture rendered during the *previous* frame and
 * folds its average luminance into the smoothed exposure estimate.
 * Reading a texture staged a frame ago (instead of the one we are about
 * to render) avoids stalling the GPU pipeline while it waits to flush. */
static void auto_exposure_read_measurement(struct auto_exposure_data *filter, float seconds)
{
	if (!filter->measure_pending)
		return;

	uint8_t *data;
	uint32_t linesize;

	if (gs_stagesurface_map(filter->measure_stagesurface, &data, &linesize)) {
		uint32_t histogram[256] = {0};
		uint32_t count = 0;

		for (uint32_t y = 0; y < MEASURE_SIZE; y++) {
			uint8_t *row = data + (size_t)y * linesize;
			for (uint32_t x = 0; x < MEASURE_SIZE; x++) {
				uint8_t r = row[x * 4 + 0];
				uint8_t g = row[x * 4 + 1];
				uint8_t b = row[x * 4 + 2];
				/* Rec. 709 relative luminance */
				int bin = (int)(0.2126 * r + 0.7152 * g + 0.0722 * b + 0.5);
				if (bin > 255)
					bin = 255;
				histogram[bin]++;
				count++;
			}
		}

		gs_stagesurface_unmap(filter->measure_stagesurface);

		/* brightness below which `percentile` % of the sampled pixels fall */
		uint32_t threshold = (uint32_t)((double)count * (double)filter->percentile / 100.0);
		uint32_t cumulative = 0;
		int bin = 0;
		for (; bin < 255; bin++) {
			cumulative += histogram[bin];
			if (cumulative >= threshold)
				break;
		}
		float measured_luma = (float)bin / 255.0f;

		if (filter->smoothed_luma < 0.0f) {
			/* first sample: snap instead of smoothing */
			filter->smoothed_luma = measured_luma;
		} else {
			/* exponential moving average with a user-configurable time constant */
			float tau = filter->speed > 0.01f ? filter->speed : 0.01f;
			float alpha = 1.0f - expf(-seconds / tau);
			filter->smoothed_luma += (measured_luma - filter->smoothed_luma) * alpha;
		}
	}

	filter->measure_pending = false;
}

/* Renders a small downscaled copy of the filter's input into
 * measure_texrender, then queues it for CPU readback next frame. */
static void auto_exposure_render_measurement_pass(struct auto_exposure_data *filter)
{
	obs_source_t *target = obs_filter_get_target(filter->context);
	if (!target)
		return;

	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);
	if (width == 0 || height == 0)
		return;

	gs_texrender_reset(filter->measure_texrender);

	if (gs_texrender_begin(filter->measure_texrender, MEASURE_SIZE, MEASURE_SIZE)) {
		struct vec4 clear_color;
		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		obs_source_video_render(target);

		gs_blend_state_pop();
		gs_texrender_end(filter->measure_texrender);
	} else {
		return;
	}

	gs_texture_t *tex = gs_texrender_get_texture(filter->measure_texrender);
	if (tex) {
		gs_stage_texture(filter->measure_stagesurface, tex);
		filter->measure_pending = true;
	}
}

/* Returns 1.0 at `full_at`, 0.0 at `zero_at`, smoothly interpolated (and
 * clamped) in between. `full_at` and `zero_at` may be given in either
 * order; the ramp direction follows automatically. */
static float fade_ramp(float value, float full_at, float zero_at)
{
	if (full_at == zero_at)
		return 1.0f; /* degenerate range: treat as always-full rather than divide by zero */

	float t = (value - full_at) / (zero_at - full_at);
	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return 1.0f - (t * t * (3.0f - 2.0f * t)); /* smoothstep, inverted */
}

static float auto_exposure_compute_gain(struct auto_exposure_data *filter)
{
	if (filter->smoothed_luma < 0.0f) {
		filter->correction_pct = 0.0f;
		return 1.0f;
	}

	float target = filter->target_brightness;
	float measured = filter->smoothed_luma < 0.01f ? 0.01f : filter->smoothed_luma;

	float raw_gain = target / measured;

	/* Auto-bypass: fade the correction to zero once the scene is already
	 * bright/dark enough that it isn't needed, independently for each
	 * direction. Off by default; when off, fade stays 1.0 so behavior is
	 * unchanged from before this feature existed. */
	float fade = 1.0f;
	if (filter->auto_bypass) {
		if (raw_gain > 1.0f)
			fade = fade_ramp(measured, filter->dark_fade_start, filter->dark_fade_end);
		else if (raw_gain < 1.0f)
			fade = fade_ramp(measured, filter->bright_fade_end, filter->bright_fade_start);
	}

	float effective_strength = filter->strength * fade;
	filter->correction_pct = effective_strength;

	/* blend between "no correction" (1.0) and the full computed gain */
	float gain = 1.0f + (raw_gain - 1.0f) * effective_strength;

	if (gain < filter->min_gain)
		gain = filter->min_gain;
	if (gain > filter->max_gain)
		gain = filter->max_gain;

	return gain;
}

static void auto_exposure_video_render(void *data, gs_effect_t *unused)
{
	UNUSED_PARAMETER(unused);
	struct auto_exposure_data *filter = data;

	if (!filter->effect) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	uint64_t now = obs_get_video_frame_time();
	float seconds = (float)(now - filter->last_time_ns) / 1000000000.0f;
	if (seconds <= 0.0f || seconds > 1.0f)
		seconds = 1.0f / 60.0f;
	filter->last_time_ns = now;

	auto_exposure_read_measurement(filter, seconds);
	filter->smoothed_gain = auto_exposure_compute_gain(filter);

	if (!obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	gs_effect_set_float(filter->param_gain, filter->smoothed_gain);
	gs_effect_set_bool(filter->param_comparison_mode, filter->comparison_mode);
	gs_effect_set_float(filter->param_split_position, filter->split_position);
	gs_effect_set_bool(filter->param_show_indicator, filter->show_indicator);
	gs_effect_set_float(filter->param_indicator_value, filter->correction_pct);

	obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

	auto_exposure_render_measurement_pass(filter);
}

struct obs_source_info auto_exposure_filter_info = {
	.id = "auto_exposure_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = auto_exposure_get_name,
	.create = auto_exposure_create,
	.destroy = auto_exposure_destroy,
	.update = auto_exposure_update,
	.get_defaults = auto_exposure_get_defaults,
	.get_properties = auto_exposure_get_properties,
	.video_render = auto_exposure_video_render,
};

bool obs_module_load(void)
{
	obs_register_source(&auto_exposure_filter_info);
	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "plugin unloaded");
}
