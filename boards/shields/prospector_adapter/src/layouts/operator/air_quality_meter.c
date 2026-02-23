#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/env_sensor_data_changed.h>

#include <lvgl.h>
#include <fonts.h>
#include "display_colors.h"
#include "air_quality_meter.h"

/* -------------------------------------------------------------------------
 * Widget dimensions
 * -------------------------------------------------------------------------
 * Container:   260 × 90 px   (same bounding box as wpm_meter)
 * Arc:          80 × 80 px   full 360° ring, arc_width = 6
 *   arc x = (260 - 80) / 2 = 90
 *   arc y = (90  - 80) / 2 = 5
 * IAQ label centered inside arc:
 *   FR_Medium_32 is ~32px tall; center_x = ARC_X + ARC_SIZE/2 = 130
 * -------------------------------------------------------------------------
 */
#define WIDGET_W    260
#define WIDGET_H     90
#define ARC_SIZE     80
#define ARC_WIDTH     6
#define ARC_X        90   /* (260 - 80) / 2 */
#define ARC_Y         5   /* (90  - 80) / 2  */

/* IAQ score → arc value mapping: arc range 0–500 */
#define IAQ_MAX     500

/* -------------------------------------------------------------------------
 * IAQ severity color lookup
 * -------------------------------------------------------------------------
 */
static lv_color_t iaq_color(uint16_t score)
{
    if (score <= 50) {
        return lv_color_hex(DISPLAY_COLOR_IAQ_GOOD);
    } else if (score <= 100) {
        return lv_color_hex(DISPLAY_COLOR_IAQ_MODERATE);
    } else if (score <= 200) {
        return lv_color_hex(DISPLAY_COLOR_IAQ_POOR);
    } else {
        return lv_color_hex(DISPLAY_COLOR_IAQ_BAD);
    }
}

/* -------------------------------------------------------------------------
 * Widget state struct (passed through ZMK_DISPLAY_WIDGET_LISTENER)
 * -------------------------------------------------------------------------
 */
struct widget_state {
    uint16_t co2_ppm;
    uint16_t tvoc_ppb;
    int16_t  temp_mdeg;
    uint16_t humidity_mpct;
    uint16_t iaq_score;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* -------------------------------------------------------------------------
 * LVGL update callback — iterates all widget instances
 * -------------------------------------------------------------------------
 */
static void update_cb(struct widget_state state)
{
    struct zmk_widget_air_quality_meter *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        /* Arc value & indicator color */
        lv_arc_set_value(widget->arc, state.iaq_score);
        lv_obj_set_style_arc_color(widget->arc, iaq_color(state.iaq_score),
                                   LV_PART_INDICATOR);

        /* IAQ center label */
        lv_label_set_text_fmt(widget->iaq_label, "%u", state.iaq_score);

        /* CO2 label: two-line, top-left */
        lv_label_set_text_fmt(widget->co2_label, "CO2\n%uppm", state.co2_ppm);

        /* TVOC label: two-line, top-right */
        lv_label_set_text_fmt(widget->tvoc_label, "TVOC\n%uppb", state.tvoc_ppb);

        /* Temperature label: single line, bottom-left
         * temp_mdeg is millidegrees C; extract whole + first decimal digit */
        int16_t temp_whole = state.temp_mdeg / 1000;
        uint16_t temp_frac = (uint16_t)((state.temp_mdeg < 0
                                         ? -state.temp_mdeg
                                         :  state.temp_mdeg) % 1000 / 100);
        lv_label_set_text_fmt(widget->temp_label, "%d.%u\xc2\xb0" "C",
                              temp_whole, temp_frac);

        /* Humidity label: single line, bottom-right
         * humidity_mpct is milli-percent */
        uint16_t hum_whole = state.humidity_mpct / 1000;
        uint16_t hum_frac  = state.humidity_mpct % 1000 / 100;
        lv_label_set_text_fmt(widget->hum_label, "%u.%u%%", hum_whole, hum_frac);
    }
}

/* -------------------------------------------------------------------------
 * Extract state from ZMK event
 * -------------------------------------------------------------------------
 */
static struct widget_state get_state(const zmk_event_t *eh)
{
    const struct zmk_env_sensor_data_changed *ev =
        as_zmk_env_sensor_data_changed(eh);
    return (struct widget_state){
        .co2_ppm       = ev->co2_ppm,
        .tvoc_ppb      = ev->tvoc_ppb,
        .temp_mdeg     = ev->temp_mdeg,
        .humidity_mpct = ev->humidity_mpct,
        .iaq_score     = ev->iaq_score,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_air_quality_meter, struct widget_state,
                             update_cb, get_state)
ZMK_SUBSCRIPTION(widget_air_quality_meter, zmk_env_sensor_data_changed);

/* -------------------------------------------------------------------------
 * Public init
 * -------------------------------------------------------------------------
 */
int zmk_widget_air_quality_meter_init(struct zmk_widget_air_quality_meter *widget,
                                      lv_obj_t *parent)
{
    /* --- Container --- */
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, WIDGET_W, WIDGET_H);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    /* --- Arc (IAQ ring) --- */
    widget->arc = lv_arc_create(widget->obj);
    lv_obj_set_size(widget->arc, ARC_SIZE, ARC_SIZE);
    lv_obj_set_pos(widget->arc, ARC_X, ARC_Y);
    lv_arc_set_range(widget->arc, 0, IAQ_MAX);
    lv_arc_set_value(widget->arc, 0);
    lv_arc_set_bg_angles(widget->arc, 0, 360);
    lv_arc_set_rotation(widget->arc, 270);
    lv_obj_set_style_arc_width(widget->arc, ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(widget->arc, ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(widget->arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(widget->arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(widget->arc,
                               lv_color_hex(DISPLAY_COLOR_IAQ_BG),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_color(widget->arc,
                               lv_color_hex(DISPLAY_COLOR_IAQ_GOOD),
                               LV_PART_INDICATOR);
    lv_obj_remove_style(widget->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(widget->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(widget->arc, LV_OPA_TRANSP, LV_PART_MAIN);

    /* --- IAQ score label (centered inside arc using absolute position)
     *     Arc center: x = ARC_X + ARC_SIZE/2 = 130, y = ARC_Y + ARC_SIZE/2 = 45
     *     Use LV_ALIGN_CENTER relative to arc object --- */
    widget->iaq_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->iaq_label, &FR_Medium_32, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->iaq_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_TEXT),
                                LV_PART_MAIN);
    lv_label_set_text(widget->iaq_label, "---");
    lv_obj_align_to(widget->iaq_label, widget->arc, LV_ALIGN_CENTER, 0, 0);

    /* --- CO2 label: top-left corner of container --- */
    widget->co2_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->co2_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->co2_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_TEXT),
                                LV_PART_MAIN);
    lv_label_set_text(widget->co2_label, "CO2\n---ppm");
    lv_obj_align(widget->co2_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* --- TVOC label: top-right corner --- */
    widget->tvoc_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->tvoc_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->tvoc_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_TEXT),
                                LV_PART_MAIN);
    lv_label_set_text(widget->tvoc_label, "TVOC\n---ppb");
    lv_obj_align(widget->tvoc_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* --- Temperature label: bottom-left corner --- */
    widget->temp_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->temp_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->temp_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_TEXT),
                                LV_PART_MAIN);
    lv_label_set_text(widget->temp_label, "--.-\xc2\xb0" "C");
    lv_obj_align(widget->temp_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* --- Humidity label: bottom-right corner --- */
    widget->hum_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->hum_label, &FG_Medium_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->hum_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_TEXT),
                                LV_PART_MAIN);
    lv_label_set_text(widget->hum_label, "--.-%%");
    lv_obj_align(widget->hum_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    /* Register and start listener */
    sys_slist_append(&widgets, &widget->node);
    widget_air_quality_meter_init();

    return 0;
}

lv_obj_t *zmk_widget_air_quality_meter_obj(struct zmk_widget_air_quality_meter *widget)
{
    return widget->obj;
}
