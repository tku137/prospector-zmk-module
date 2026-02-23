#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_air_quality_meter {
    sys_snode_t node;
    lv_obj_t   *obj;
    lv_obj_t   *arc;
    lv_obj_t   *iaq_label;
    lv_obj_t   *co2_label;
    lv_obj_t   *tvoc_label;
    lv_obj_t   *temp_label;
    lv_obj_t   *hum_label;
};

int       zmk_widget_air_quality_meter_init(struct zmk_widget_air_quality_meter *widget,
                                            lv_obj_t *parent);
lv_obj_t *zmk_widget_air_quality_meter_obj(struct zmk_widget_air_quality_meter *widget);
