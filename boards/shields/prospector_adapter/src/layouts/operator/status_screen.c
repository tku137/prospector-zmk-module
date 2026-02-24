#include <lvgl.h>

#include "modifier_indicator.h"
#include "layer_display.h"
#include "battery_circles.h"
#include "output.h"

#if IS_ENABLED(CONFIG_PROSPECTOR_ENV_SENSOR)
#include "air_quality_meter.h"
#else
#include "wpm_meter.h"
#endif

#include <fonts.h>

static struct zmk_widget_modifier_indicator modifier_indicator_widget;
static struct zmk_widget_layer_display layer_display_widget;
static struct zmk_widget_battery_circles battery_circles_widget;
static struct zmk_widget_output output_widget;

#if IS_ENABLED(CONFIG_PROSPECTOR_ENV_SENSOR)
static struct zmk_widget_air_quality_meter center_widget;
#else
static struct zmk_widget_wpm_meter center_widget;
#endif

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);

    zmk_widget_modifier_indicator_init(&modifier_indicator_widget, screen);
    lv_obj_set_pos(zmk_widget_modifier_indicator_obj(&modifier_indicator_widget), 25, 8);

#if IS_ENABLED(CONFIG_PROSPECTOR_ENV_SENSOR)
    zmk_widget_air_quality_meter_init(&center_widget, screen);
    lv_obj_set_pos(zmk_widget_air_quality_meter_obj(&center_widget), 10, 42);
#else
    zmk_widget_wpm_meter_init(&center_widget, screen);
    lv_obj_set_pos(zmk_widget_wpm_meter_obj(&center_widget), 10, 42);
#endif

    zmk_widget_layer_display_init(&layer_display_widget, screen);
    lv_obj_set_pos(zmk_widget_layer_display_obj(&layer_display_widget), 10, 152);

    zmk_widget_battery_circles_init(&battery_circles_widget, screen);
    lv_obj_set_pos(zmk_widget_battery_circles_obj(&battery_circles_widget), 11, 180);

    zmk_widget_output_init(&output_widget, screen);
    lv_obj_set_pos(zmk_widget_output_obj(&output_widget), 148, 180);

    return screen;
}
