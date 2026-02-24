#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <ctype.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/env_sensor_data_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

#include <lvgl.h>
#include <fonts.h>
#include "display_colors.h"
#include "air_quality_meter.h"

/* -------------------------------------------------------------------------
 * Widget dimensions
 * -------------------------------------------------------------------------
 * Container:   260 × 100 px
 *
 * Arc:          80 ×  80 px, full 360° ring, arc_width = 6
 *   arc x = 55  (widget x=10, screen x=65; arc right edge=135 in widget
 *               = screen x=145, aligns with output widget left edge x=148)
 *   arc y =  5  ((90 - 80) / 2)
 *
 * Left column (x=0):
 *   Temp  — top-left,    FG_Medium_21
 *   Hum   — bottom-left, FG_Medium_21
 *
 * Arc center label:
 *   "IAQ" static text, FG_Medium_21, centered inside arc
 *
 * Right column (x=143, arc right edge 135 + 8px gap):
 *   CO2   — y=0,  one line "CO2: XXXXX",  FG_Medium_21
 *   TVOC  — y=28, one line "TVOC: XXXXX", FG_Medium_21
 *
 * Layer name — overflows bottom-right, identical to wpm_meter styling
 * -------------------------------------------------------------------------
 */
#define WIDGET_W      260
#define WIDGET_H       90
#define ARC_SIZE       80
#define ARC_WIDTH       6
#define ARC_X          55
#define ARC_Y           5   /* (90 - 80) / 2 */
#define RIGHT_COL_X   143   /* ARC_X + ARC_SIZE + 8 */
#define IAQ_MAX       500

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
 * Widget state structs
 * -------------------------------------------------------------------------
 */
struct widget_state {
    uint16_t co2_ppm;
    uint16_t tvoc_ppb;
    int16_t  temp_mdeg;
    uint16_t humidity_mpct;
    uint16_t iaq_score;
};

struct layer_state {
    uint8_t index;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/* -------------------------------------------------------------------------
 * LVGL update callback — sensor data
 * -------------------------------------------------------------------------
 */
static void update_cb(struct widget_state state)
{
    struct zmk_widget_air_quality_meter *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        /* Arc indicator color */
        lv_obj_set_style_arc_color(widget->arc, iaq_color(state.iaq_score),
                                   LV_PART_INDICATOR);

        /* CO2 label: single line, "CO2: XXXXX" */
        lv_label_set_text_fmt(widget->co2_label, "CO2: %u", state.co2_ppm);

        /* TVOC label: single line, "TVOC: XXXXX" */
        lv_label_set_text_fmt(widget->tvoc_label, "TVOC: %u", state.tvoc_ppb);

        /* Temperature: "24.1C" — no degree glyph, not in font */
        int16_t temp_whole = state.temp_mdeg / 1000;
        uint16_t temp_frac = (uint16_t)((state.temp_mdeg < 0
                                         ? -state.temp_mdeg
                                         :  state.temp_mdeg) % 1000 / 100);
        lv_label_set_text_fmt(widget->temp_label, "%d.%uC", temp_whole, temp_frac);

        /* Humidity: "47.3%" */
        uint16_t hum_whole = state.humidity_mpct / 1000;
        uint16_t hum_frac  = state.humidity_mpct % 1000 / 100;
        lv_label_set_text_fmt(widget->hum_label, "%u.%u%%", hum_whole, hum_frac);
    }
}

/* -------------------------------------------------------------------------
 * LVGL update callback — layer name
 * -------------------------------------------------------------------------
 */
static void layer_update_cb(struct layer_state state)
{
    struct zmk_widget_air_quality_meter *widget;

    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        const char *layer_name =
            zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(state.index));
        char display_name[32];

        if (layer_name && *layer_name) {
            snprintf(display_name, sizeof(display_name), "%s", layer_name);
        } else {
            snprintf(display_name, sizeof(display_name), "Layer %d", state.index);
        }

#if IS_ENABLED(CONFIG_PROSPECTOR_LAYER_NAME_UPPERCASE)
        for (int i = 0; display_name[i]; i++) {
            display_name[i] = toupper((unsigned char)display_name[i]);
        }
#endif

        lv_label_set_text(widget->layer_label, display_name);
    }
}

/* -------------------------------------------------------------------------
 * Extract state from ZMK events
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

static struct layer_state layer_get_state(const zmk_event_t *eh)
{
    return (struct layer_state){
        .index = zmk_keymap_highest_layer_active(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_air_quality_meter, struct widget_state,
                             update_cb, get_state)
ZMK_SUBSCRIPTION(widget_air_quality_meter, zmk_env_sensor_data_changed);

ZMK_DISPLAY_WIDGET_LISTENER(widget_air_quality_meter_layer, struct layer_state,
                             layer_update_cb, layer_get_state)
ZMK_SUBSCRIPTION(widget_air_quality_meter_layer, zmk_layer_state_changed);

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

    /* --- "IAQ" static label centered inside arc --- */
    widget->iaq_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->iaq_label, &FG_Medium_21, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->iaq_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_LABEL),
                                LV_PART_MAIN);
    lv_label_set_text(widget->iaq_label, "IAQ");
    lv_obj_align_to(widget->iaq_label, widget->arc, LV_ALIGN_CENTER, 0, 0);

    /* --- Temperature label: top-left --- */
    widget->temp_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->temp_label, &FG_Medium_21, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->temp_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_LABEL),
                                LV_PART_MAIN);
    lv_label_set_text(widget->temp_label, "-C");
    lv_obj_align(widget->temp_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* --- Humidity label: bottom-left --- */
    widget->hum_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->hum_label, &FG_Medium_21, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->hum_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_LABEL),
                                LV_PART_MAIN);
    lv_label_set_text(widget->hum_label, "-%");
    lv_obj_align(widget->hum_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* --- CO2 label: top-right column, single line --- */
    widget->co2_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->co2_label, &FG_Medium_21, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->co2_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_LABEL),
                                LV_PART_MAIN);
    lv_label_set_text(widget->co2_label, "CO2: -");
    lv_obj_set_pos(widget->co2_label, RIGHT_COL_X, 0);

    /* --- TVOC label: below CO2, single line --- */
    widget->tvoc_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_font(widget->tvoc_label, &FG_Medium_21, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->tvoc_label,
                                lv_color_hex(DISPLAY_COLOR_IAQ_LABEL),
                                LV_PART_MAIN);
    lv_label_set_text(widget->tvoc_label, "TVOC: -");
    lv_obj_set_pos(widget->tvoc_label, RIGHT_COL_X, 28);

    /* --- Layer name label: overflows bottom-right, identical to wpm_meter --- */
    widget->layer_label = lv_label_create(widget->obj);
    lv_label_set_text(widget->layer_label, "");
    lv_obj_set_style_text_font(widget->layer_label,
                               &DINishExpanded_Light_36, LV_PART_MAIN);
    lv_obj_set_style_text_color(widget->layer_label,
                                lv_color_hex(DISPLAY_COLOR_LAYER_TEXT),
                                LV_PART_MAIN);
    lv_obj_set_style_bg_color(widget->layer_label,
                              lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->layer_label, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(widget->layer_label, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(widget->layer_label, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(widget->layer_label, 3, LV_PART_MAIN);
    lv_obj_align(widget->layer_label, LV_ALIGN_BOTTOM_RIGHT, 9, 7);

    /* Register and start listeners */
    sys_slist_append(&widgets, &widget->node);
    widget_air_quality_meter_init();
    widget_air_quality_meter_layer_init();

    return 0;
}

lv_obj_t *zmk_widget_air_quality_meter_obj(struct zmk_widget_air_quality_meter *widget)
{
    return widget->obj;
}
