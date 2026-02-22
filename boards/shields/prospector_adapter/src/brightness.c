#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/led.h>
#include <zephyr/sys/printk.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(prospector_brightness, CONFIG_LOG_DEFAULT_LEVEL);

#if IS_ENABLED(CONFIG_PROSPECTOR_BRIGHTNESS_KEYBOARD_CONTROL)
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#endif

static const struct device *pwm_leds_dev = DEVICE_DT_GET_ONE(pwm_leds);
#define DISP_BL DT_NODE_CHILD_IDX(DT_NODELABEL(disp_bl))

/* -------------------------------------------------------------------------
 * Keyboard brightness modifier state
 * -------------------------------------------------------------------------
 * brightness_modifier: signed delta applied on top of the base brightness
 *   (ALS-managed or fixed). Lets the user trim brightness without disturbing
 *   the ALS base value.
 * screen_on: when false the display is set to 0% regardless of base/modifier.
 * -------------------------------------------------------------------------
 */
#if IS_ENABLED(CONFIG_PROSPECTOR_BRIGHTNESS_KEYBOARD_CONTROL)
static int8_t brightness_modifier = 0;
static bool screen_on = true;
#endif

/* -------------------------------------------------------------------------
 * apply_brightness() — the single call site that drives the PWM hardware.
 * All paths (ALS, fixed, keyboard) funnel through here.
 * -------------------------------------------------------------------------
 */
static void apply_brightness(uint8_t value)
{
	led_set_brightness(pwm_leds_dev, DISP_BL, value);
}

/* -------------------------------------------------------------------------
 * ALS path
 * -------------------------------------------------------------------------
 */
#ifdef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR

static uint8_t current_brightness = 100;

#define SENSOR_MIN		0
#define SENSOR_MAX		100
#define PWM_MIN			1
#define PWM_MAX			100

#define FADE_STEP		1
#define FADE_SLEEP_BRIGHTEN_MS	3
#define FADE_SLEEP_DARKEN_MS	10
#define FADE_THRESHOLD		10

#define NORMAL_SAMPLE_SLEEP_MS	100

#define BURST_SAMPLE_SLEEP_MS	30
#define BURST_SAMPLE_TIMEOUT	10
#define BURST_SAMPLE_CONSECUTIVE 3

static uint8_t map_light_to_pwm(int32_t sensor_reading)
{
	if (sensor_reading < SENSOR_MIN) {
		return PWM_MIN;
	}
	if (sensor_reading > SENSOR_MAX) {
		sensor_reading = SENSOR_MAX;
	}
	return (uint8_t)(PWM_MIN + ((PWM_MAX - PWM_MIN) *
		(sensor_reading - SENSOR_MIN)) / (SENSOR_MAX - SENSOR_MIN));
}

/*
 * bl_fade() — step current_brightness toward target, driving the display at
 * each step.  When keyboard control is active the effective output is
 * current_brightness + modifier (clamped), so the user's trim is preserved
 * during ALS transitions.
 */
static void bl_fade(uint8_t source, uint8_t target)
{
	bool increasing = target > source;

	current_brightness = source;

	while ((increasing  && current_brightness < target) ||
	       (!increasing && current_brightness > target)) {

		current_brightness += increasing ? FADE_STEP : (uint8_t)(-FADE_STEP);

		if (current_brightness > 100) {
			current_brightness = 100;
		}

#if IS_ENABLED(CONFIG_PROSPECTOR_BRIGHTNESS_KEYBOARD_CONTROL)
		if (!screen_on) {
			/* Don't touch the PWM while toggled off — just track base. */
			k_msleep(increasing ? FADE_SLEEP_BRIGHTEN_MS : FADE_SLEEP_DARKEN_MS);
			continue;
		}
		int effective = (int)current_brightness + brightness_modifier;
		if (effective < 1)   effective = 1;
		if (effective > 100) effective = 100;
		apply_brightness((uint8_t)effective);
#else
		apply_brightness(current_brightness);
#endif

		k_msleep(increasing ? FADE_SLEEP_BRIGHTEN_MS : FADE_SLEEP_DARKEN_MS);
	}
}

extern void als_thread(void *d0, void *d1, void *d2)
{
	ARG_UNUSED(d0);
	ARG_UNUSED(d1);
	ARG_UNUSED(d2);

	const struct device *dev;
	struct sensor_value intensity;
	uint8_t mapped_brightness;

	dev = DEVICE_DT_GET_ONE(avago_apds9960);
	if (!device_is_ready(dev)) {
		LOG_ERR("ALS sensor: device not ready");
		return;
	}

	while (1) {
		k_msleep(NORMAL_SAMPLE_SLEEP_MS);

		if (sensor_sample_fetch(dev)) {
			LOG_ERR("sensor_sample_fetch failed");
			continue;
		}
		if (sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &intensity)) {
			LOG_ERR("Cannot read ALS data");
			continue;
		}

		mapped_brightness = map_light_to_pwm(intensity.val1);

		if (abs(mapped_brightness - current_brightness) > FADE_THRESHOLD) {
			uint8_t integrator = 0;

			for (int i = 0; i < BURST_SAMPLE_TIMEOUT; i++) {
				k_msleep(BURST_SAMPLE_SLEEP_MS);

				if (sensor_sample_fetch(dev)) {
					LOG_ERR("sensor_sample_fetch failed");
					continue;
				}
				if (sensor_channel_get(dev, SENSOR_CHAN_LIGHT, &intensity)) {
					LOG_ERR("Cannot read ALS data");
					continue;
				}

				mapped_brightness = map_light_to_pwm(intensity.val1);

				if (abs(mapped_brightness - current_brightness) > FADE_THRESHOLD) {
					integrator++;
					if (integrator >= BURST_SAMPLE_CONSECUTIVE) {
						bl_fade(current_brightness, mapped_brightness);
						break;
					}
				}
			}
		}
	}
}

K_THREAD_DEFINE(als_tid, 1024, als_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

/* -------------------------------------------------------------------------
 * Fixed brightness path
 * -------------------------------------------------------------------------
 */
#else

static int init_fixed_brightness(void)
{
#if IS_ENABLED(CONFIG_PROSPECTOR_BRIGHTNESS_KEYBOARD_CONTROL)
	/* With keyboard control the modifier starts at 0, so effective ==
	 * FIXED_BRIGHTNESS on boot.  Route through apply_brightness() so the
	 * same code path is used for all subsequent keyboard adjustments. */
	int effective = (int)CONFIG_PROSPECTOR_FIXED_BRIGHTNESS + brightness_modifier;
	if (effective < 1)   effective = 1;
	if (effective > 100) effective = 100;
	apply_brightness((uint8_t)effective);
#else
	apply_brightness(CONFIG_PROSPECTOR_FIXED_BRIGHTNESS);
#endif
	return 0;
}

SYS_INIT(init_fixed_brightness, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR */

/* -------------------------------------------------------------------------
 * Keyboard brightness control
 * -------------------------------------------------------------------------
 * The modifier is a signed delta on top of the ALS/fixed base brightness.
 * Increasing/decreasing clamps so the effective value stays in [1, 100].
 * Toggle drives the PWM to 0 (off) or restores the effective value (on).
 * -------------------------------------------------------------------------
 */
#if IS_ENABLED(CONFIG_PROSPECTOR_BRIGHTNESS_KEYBOARD_CONTROL)

/*
 * Return the current base brightness from whichever path is active.
 * For ALS: the live current_brightness variable.
 * For fixed: the compile-time constant.
 */
static uint8_t get_base_brightness(void)
{
#ifdef CONFIG_PROSPECTOR_USE_AMBIENT_LIGHT_SENSOR
	return current_brightness;
#else
	return (uint8_t)CONFIG_PROSPECTOR_FIXED_BRIGHTNESS;
#endif
}

/*
 * Compute and apply the effective brightness:
 *   effective = clamp(base + modifier, 1, 100)
 * If the display is toggled off, set PWM to 0 instead.
 */
static void apply_effective_brightness(void)
{
	if (!screen_on) {
		apply_brightness(0);
		return;
	}
	int effective = (int)get_base_brightness() + brightness_modifier;
	if (effective < 1)   effective = 1;
	if (effective > 100) effective = 100;
	apply_brightness((uint8_t)effective);
}

static void increase_brightness(void)
{
	/* Clamp so that base + modifier never exceeds 100 */
	int headroom = 100 - (int)get_base_brightness();
	int new_mod = (int)brightness_modifier + CONFIG_PROSPECTOR_BRIGHTNESS_STEP;
	if (new_mod > headroom) {
		new_mod = headroom;
	}
	brightness_modifier = (int8_t)new_mod;
	apply_effective_brightness();
	LOG_INF("Brightness up: modifier=%d effective=%d",
		brightness_modifier, (int)get_base_brightness() + brightness_modifier);
}

static void decrease_brightness(void)
{
	/* Clamp so that effective never goes below 1 */
	int floor = 1 - (int)get_base_brightness();
	int new_mod = (int)brightness_modifier - CONFIG_PROSPECTOR_BRIGHTNESS_STEP;
	if (new_mod < floor) {
		new_mod = floor;
	}
	brightness_modifier = (int8_t)new_mod;
	apply_effective_brightness();
	LOG_INF("Brightness down: modifier=%d effective=%d",
		brightness_modifier, (int)get_base_brightness() + brightness_modifier);
}

static void toggle_brightness(void)
{
	screen_on = !screen_on;
	apply_effective_brightness();
	LOG_INF("Display toggled %s", screen_on ? "on" : "off");
}

static int key_listener(const zmk_event_t *eh)
{
	const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);

	if (ev == NULL || !ev->state) {
		/* Ignore key-up events */
		return ZMK_EV_EVENT_BUBBLE;
	}

	if (ev->keycode == CONFIG_PROSPECTOR_BRIGHTNESS_UP_KEYCODE) {
		increase_brightness();
	} else if (ev->keycode == CONFIG_PROSPECTOR_BRIGHTNESS_DOWN_KEYCODE) {
		decrease_brightness();
	} else if (ev->keycode == CONFIG_PROSPECTOR_BRIGHTNESS_TOGGLE_KEYCODE) {
		toggle_brightness();
	}

	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(prospector_brightness, key_listener);
ZMK_SUBSCRIPTION(prospector_brightness, zmk_keycode_state_changed);

#endif /* CONFIG_PROSPECTOR_BRIGHTNESS_KEYBOARD_CONTROL */
