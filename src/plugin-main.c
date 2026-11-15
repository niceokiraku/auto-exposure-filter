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
#define SETTING_METER_WIDTH "meter_width"
#define SETTING_AUTO_BYPASS "auto_bypass"
#define SETTING_DARK_FADE_START "dark_fade_start"
#define SETTING_DARK_FADE_END "dark_fade_end"
#define SETTING_BRIGHT_FADE_START "bright_fade_start"
#define SETTING_BRIGHT_FADE_END "bright_fade_end"
#define SETTING_SHOW_INDICATOR "show_indicator"

/* Side length (in pixels) of the small render target used to estimate
 * scene brightness. 32x32 = 1024 samples, enough for a stable percentile. */
#define MEASURE_SIZE 32

/* Scenes darker than this all get the same (maximum) correction. */
#define MIN_LUMA 0.01f

/* If the filter has not been rendered for this long (source hidden, scene
 * switched away), the last measurement no longer describes what is on screen. */
#define STALE_SECONDS 0.5f

struct auto_exposure_data {
	obs_source_t *context;

	gs_effect_t *effect;
	gs_eparam_t *param_image;
	gs_eparam_t *param_gain;
	gs_eparam_t *param_comparison_mode;
	gs_eparam_t *param_split_position;
	gs_eparam_t *param_show_indicator;
	gs_eparam_t *param_indicator_value;

	gs_texrender_t *input_texrender;
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
	float meter_width;
	bool auto_bypass;
	float dark_fade_start;
	float dark_fade_end;
	float bright_fade_start;
	float bright_fade_end;
	bool show_indicator;

	/* running state */
	float smoothed_log_luma;
	bool has_measurement;
	float gain;
	float correction_pct; /* 0-1, how much correction is being applied right now */

	/* The source can be drawn several times per frame (preview + program,
	 * the filter dialog's own preview). video_tick marks the start of a
	 * frame so the input is captured and measured exactly once per frame. */
	float pending_seconds;
	bool frame_fresh;
	bool input_valid;
	bool capturing;
	uint32_t input_cx;
	uint32_t input_cy;
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
	filter->meter_width = (float)obs_data_get_double(settings, SETTING_METER_WIDTH);
	filter->auto_bypass = obs_data_get_bool(settings, SETTING_AUTO_BYPASS);
	filter->dark_fade_start = (float)obs_data_get_double(settings, SETTING_DARK_FADE_START);
	filter->dark_fade_end = (float)obs_data_get_double(settings, SETTING_DARK_FADE_END);
	filter->bright_fade_start = (float)obs_data_get_double(settings, SETTING_BRIGHT_FADE_START);
	filter->bright_fade_end = (float)obs_data_get_double(settings, SETTING_BRIGHT_FADE_END);
	filter->show_indicator = obs_data_get_bool(settings, SETTING_SHOW_INDICATOR);
}

static void auto_exposure_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, SETTING_TARGET_BRIGHTNESS, 0.40);
	obs_data_set_default_double(settings, SETTING_STRENGTH, 0.80);
	obs_data_set_default_double(settings, SETTING_SPEED, 0.20);
	obs_data_set_default_double(settings, SETTING_MIN_GAIN, 0.20);
	obs_data_set_default_double(settings, SETTING_MAX_GAIN, 2.50);
	obs_data_set_default_bool(settings, SETTING_COMPARISON_MODE, false);
	obs_data_set_default_double(settings, SETTING_SPLIT_POSITION, 0.5);
	obs_data_set_default_double(settings, SETTING_PERCENTILE, 50.0);
	obs_data_set_default_double(settings, SETTING_METER_WIDTH, 0.0);
	obs_data_set_default_bool(settings, SETTING_AUTO_BYPASS, false);
	obs_data_set_default_double(settings, SETTING_DARK_FADE_START, 0.40);
	obs_data_set_default_double(settings, SETTING_DARK_FADE_END, 0.50);
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
	obs_properties_add_float_slider(props, SETTING_METER_WIDTH, obs_module_text("AutoExposure.MeterWidth"), 0.0,
					25.0, 1.0);

	obs_properties_add_float_slider(props, SETTING_TARGET_BRIGHTNESS,
					obs_module_text("AutoExposure.TargetBrightness"), 0.1, 0.9, 0.01);
	obs_properties_add_float_slider(props, SETTING_STRENGTH, obs_module_text("AutoExposure.Strength"), 0.0, 1.0,
					0.01);
	obs_properties_add_float_slider(props, SETTING_SPEED, obs_module_text("AutoExposure.Speed"), 0.2, 8.0, 0.1);
	obs_properties_add_float_slider(props, SETTING_MIN_GAIN, obs_module_text("AutoExposure.MinGain"), 0.2, 1.0,
					0.01);
	obs_properties_add_float_slider(props, SETTING_MAX_GAIN, obs_module_text("AutoExposure.MaxGain"), 1.0, 4.0,
					0.01);

	/* Checkable groups: the group's checkbox is the bool setting itself, and
	 * its sliders are greyed out while the feature is off. */
	obs_properties_t *bypass = obs_properties_create();
	obs_properties_add_text(bypass, "dark_fade_info", obs_module_text("AutoExposure.DarkFadeInfo"), OBS_TEXT_INFO);
	obs_properties_add_float_slider(bypass, SETTING_DARK_FADE_START, obs_module_text("AutoExposure.DarkFadeStart"),
					0.0, 1.0, 0.01);
	obs_properties_add_float_slider(bypass, SETTING_DARK_FADE_END, obs_module_text("AutoExposure.DarkFadeEnd"), 0.0,
					1.0, 0.01);
	obs_properties_add_text(bypass, "bright_fade_info", obs_module_text("AutoExposure.BrightFadeInfo"),
				OBS_TEXT_INFO);
	obs_properties_add_float_slider(bypass, SETTING_BRIGHT_FADE_START,
					obs_module_text("AutoExposure.BrightFadeStart"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(bypass, SETTING_BRIGHT_FADE_END, obs_module_text("AutoExposure.BrightFadeEnd"),
					0.0, 1.0, 0.01);
	obs_properties_add_group(props, SETTING_AUTO_BYPASS, obs_module_text("AutoExposure.AutoBypass"),
				 OBS_GROUP_CHECKABLE, bypass);

	obs_properties_add_bool(props, SETTING_SHOW_INDICATOR, obs_module_text("AutoExposure.ShowIndicator"));

	obs_properties_t *comparison = obs_properties_create();
	obs_properties_add_float_slider(comparison, SETTING_SPLIT_POSITION,
					obs_module_text("AutoExposure.SplitPosition"), 0.05, 0.95, 0.01);
	obs_properties_add_group(props, SETTING_COMPARISON_MODE, obs_module_text("AutoExposure.ComparisonMode"),
				 OBS_GROUP_CHECKABLE, comparison);

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

	filter->input_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->measure_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	filter->measure_stagesurface = gs_stagesurface_create(MEASURE_SIZE, MEASURE_SIZE, GS_RGBA);
	obs_leave_graphics();

	bfree(effect_path);

	filter->gain = 1.0f;

	auto_exposure_update(filter, settings);

	return filter;
}

static void auto_exposure_destroy(void *data)
{
	struct auto_exposure_data *filter = data;

	obs_enter_graphics();
	if (filter->effect)
		gs_effect_destroy(filter->effect);
	if (filter->input_texrender)
		gs_texrender_destroy(filter->input_texrender);
	if (filter->measure_texrender)
		gs_texrender_destroy(filter->measure_texrender);
	if (filter->measure_stagesurface)
		gs_stagesurface_destroy(filter->measure_stagesurface);
	obs_leave_graphics();

	bfree(filter);
}

static void auto_exposure_video_tick(void *data, float seconds)
{
	struct auto_exposure_data *filter = data;

	filter->pending_seconds += seconds;
	filter->frame_fresh = true;
}

/* Brightness (0-1) below which `percentile` % of the samples fall. */
static float histogram_percentile(const uint32_t *histogram, uint32_t total, float percentile)
{
	uint32_t threshold = (uint32_t)((double)total * (double)percentile / 100.0);
	uint32_t cumulative = 0;
	int bin = 0;
	for (; bin < 255; bin++) {
		cumulative += histogram[bin];
		if (cumulative >= threshold)
			break;
	}
	return (float)bin / 255.0f;
}

/* Mean brightness (0-1) of the samples ranked between two percentiles. A
 * single percentile jumps between the two peaks of a half-dark, half-bright
 * picture when their balance shifts slightly; averaging a band around it
 * moves gradually instead. */
static float histogram_range_mean(const uint32_t *histogram, uint32_t total, float low_percentile,
				  float high_percentile)
{
	if (low_percentile < 0.0f)
		low_percentile = 0.0f;
	if (high_percentile > 100.0f)
		high_percentile = 100.0f;

	double low = (double)total * (double)low_percentile / 100.0;
	double high = (double)total * (double)high_percentile / 100.0;
	double cumulative = 0.0;
	double sum = 0.0;
	double weight = 0.0;

	for (int bin = 0; bin < 256; bin++) {
		double next = cumulative + (double)histogram[bin];
		double overlap = (next < high ? next : high) - (cumulative > low ? cumulative : low);
		if (overlap > 0.0) {
			sum += overlap * (double)bin;
			weight += overlap;
		}
		cumulative = next;
	}

	if (weight <= 0.0)
		return histogram_percentile(histogram, total, (low_percentile + high_percentile) * 0.5f);
	return (float)(sum / weight / 255.0);
}

/* Reads back the tiny texture staged during the *previous* frame and folds
 * its brightness into the smoothed exposure estimate. Reading a texture
 * staged a frame ago (instead of the one about to be rendered) avoids
 * stalling the GPU pipeline while it waits to flush. */
static void auto_exposure_read_measurement(struct auto_exposure_data *filter, float seconds)
{
	if (seconds > STALE_SECONDS) {
		/* start over: snap to the next measurement instead of easing
		 * in from whatever was on screen before the gap */
		filter->measure_pending = false;
		filter->has_measurement = false;
	}

	if (!filter->measure_pending)
		return;
	filter->measure_pending = false;

	uint8_t *data;
	uint32_t linesize;

	if (!gs_stagesurface_map(filter->measure_stagesurface, &data, &linesize))
		return;

	/* The texture is premultiplied, so each sample is un-premultiplied and
	 * weighted by its alpha: transparent areas don't count as black. */
	uint32_t histogram[256] = {0};
	uint32_t total = 0;

	for (uint32_t y = 0; y < MEASURE_SIZE; y++) {
		uint8_t *row = data + (size_t)y * linesize;
		for (uint32_t x = 0; x < MEASURE_SIZE; x++) {
			uint32_t a = row[x * 4 + 3];
			if (a == 0)
				continue;

			/* Rec. 709 luma of the gamma-encoded pixel */
			float luma = 0.2126f * row[x * 4 + 0] + 0.7152f * row[x * 4 + 1] + 0.0722f * row[x * 4 + 2];
			int bin = (int)(luma * 255.0f / (float)a + 0.5f);
			if (bin > 255)
				bin = 255;
			histogram[bin] += a;
			total += a;
		}
	}

	gs_stagesurface_unmap(filter->measure_stagesurface);

	if (total == 0)
		return;

	float measured_luma = filter->meter_width > 0.0f
				      ? histogram_range_mean(histogram, total, filter->percentile - filter->meter_width,
							     filter->percentile + filter->meter_width)
				      : histogram_percentile(histogram, total, filter->percentile);
	if (measured_luma < MIN_LUMA)
		measured_luma = MIN_LUMA;

	/* Smoothed in the log domain (i.e. in exposure stops) so that getting
	 * darker and getting brighter are tracked at the same rate. */
	float log_luma = logf(measured_luma);

	if (!filter->has_measurement) {
		filter->smoothed_log_luma = log_luma;
		filter->has_measurement = true;
	} else {
		float tau = filter->speed > 0.01f ? filter->speed : 0.01f;
		float alpha = 1.0f - expf(-seconds / tau);
		filter->smoothed_log_luma += (log_luma - filter->smoothed_log_luma) * alpha;
	}
}

/* Renders the filter's input once into input_texrender. Both the final
 * output and the measurement are drawn from this texture, so the source
 * (and any filters before this one) is only rendered once per frame. */
static bool auto_exposure_capture_input(struct auto_exposure_data *filter, obs_source_t *target, obs_source_t *parent)
{
	uint32_t cx = obs_source_get_base_width(target);
	uint32_t cy = obs_source_get_base_height(target);
	if (cx == 0 || cy == 0)
		return false;

	gs_texrender_reset(filter->input_texrender);
	if (!gs_texrender_begin_with_color_space(filter->input_texrender, cx, cy, GS_CS_SRGB))
		return false;

	uint32_t parent_flags = obs_source_get_output_flags(parent);
	bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
	bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;

	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	if (target == parent && !custom_draw && !async)
		obs_source_default_render(target);
	else
		obs_source_video_render(target);

	gs_blend_state_pop();
	gs_texrender_end(filter->input_texrender);

	filter->input_cx = cx;
	filter->input_cy = cy;
	return true;
}

/* Downscales the captured input into measure_texrender and queues it for
 * CPU readback next frame. */
static void auto_exposure_stage_measurement(struct auto_exposure_data *filter)
{
	gs_texture_t *input = gs_texrender_get_texture(filter->input_texrender);
	if (!input)
		return;

	gs_texrender_reset(filter->measure_texrender);
	if (!gs_texrender_begin(filter->measure_texrender, MEASURE_SIZE, MEASURE_SIZE))
		return;

	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)filter->input_cx, 0.0f, (float)filter->input_cy, -100.0f, 100.0f);

	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	gs_effect_t *copy = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(copy, "image"), input);
	while (gs_effect_loop(copy, "Draw"))
		gs_draw_sprite(input, 0, filter->input_cx, filter->input_cy);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
	gs_texrender_end(filter->measure_texrender);

	gs_texture_t *measured = gs_texrender_get_texture(filter->measure_texrender);
	if (measured) {
		gs_stage_texture(filter->measure_stagesurface, measured);
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
	if (!filter->has_measurement) {
		filter->correction_pct = 0.0f;
		return 1.0f;
	}

	float target = filter->target_brightness;
	float measured = expf(filter->smoothed_log_luma);

	float raw_gain = target / measured;

	/* Auto-bypass: fade the correction to zero once the scene is already
	 * bright/dark enough that it isn't needed, independently for each
	 * direction. Brightening only ever happens below the target and
	 * darkening only above it, so each ramp only matters on its own side. */
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

	obs_source_t *target = obs_filter_get_target(filter->context);
	obs_source_t *parent = obs_filter_get_parent(filter->context);

	if (!filter->effect || !target || !parent || filter->capturing) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	if (filter->frame_fresh) {
		float seconds = filter->pending_seconds;
		filter->frame_fresh = false;
		filter->pending_seconds = 0.0f;

		auto_exposure_read_measurement(filter, seconds);
		filter->gain = auto_exposure_compute_gain(filter);

		filter->capturing = true;
		filter->input_valid = auto_exposure_capture_input(filter, target, parent);
		filter->capturing = false;

		if (filter->input_valid)
			auto_exposure_stage_measurement(filter);
	}

	gs_texture_t *input = filter->input_valid ? gs_texrender_get_texture(filter->input_texrender) : NULL;
	if (!input) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	gs_effect_set_texture(filter->param_image, input);
	gs_effect_set_float(filter->param_gain, filter->gain);
	gs_effect_set_bool(filter->param_comparison_mode, filter->comparison_mode);
	gs_effect_set_float(filter->param_split_position, filter->split_position);
	gs_effect_set_bool(filter->param_show_indicator, filter->show_indicator);
	gs_effect_set_float(filter->param_indicator_value, filter->correction_pct);

	const bool previous_srgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	while (gs_effect_loop(filter->effect, "Draw"))
		gs_draw_sprite(input, 0, filter->input_cx, filter->input_cy);

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previous_srgb);
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
	.video_tick = auto_exposure_video_tick,
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
