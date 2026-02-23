# AGENTS.md — Coding Agent Guide for prospector-zmk-module-tku137

## Project Overview

This is a **ZMK firmware module** providing custom status screen support for the
[Prospector](https://github.com/carrefinho/prospector) display dongle (an ST7789V
TFT display peripheral for ZMK keyboards). The module overrides ZMK's built-in
caps_word behavior and ST7789V display driver, adds custom events, and provides
four selectable LVGL-based display layouts: `classic`, `field`, `operator`, `radii`.

All source is **C** (`.c`/`.h`). No C++, no scripting languages in the build.

---

## Build System

This module is **not a standalone project**. It integrates into a West/Zephyr
workspace as a ZMK module. There is no local `make`, `ninja`, or `cmake` invocation.

### Workspace Setup

```sh
west init -l config
west update
```

### Building (from a configured West workspace)

```sh
west build -s zmk/app -b xiao_ble//zmk -- \
  -DSHIELD="<keyboard_shield>_dongle prospector_adapter" \
  -DZMK_CONFIG=/path/to/your/config
```

The feature gate for all module code is `CONFIG_SHIELD_PROSPECTOR_ADAPTER`.

### Key Build Files

| File | Purpose |
|---|---|
| `zephyr/module.yml` | Registers module with Zephyr; declares `cmake`, `kconfig`, `board_root`, `dts_root` |
| `config/west.yml` | West manifest; pulls ZMK from `zmkfirmware/zmk@main` |
| `CMakeLists.txt` | Root CMake; conditionally adds driver override and src files |
| `boards/shields/prospector_adapter/CMakeLists.txt` | Adds widget/font/feature sources per layout |
| `Kconfig` | All user-facing `CONFIG_PROSPECTOR_*` options |
| `Kconfig.defconfig` | Hardware subsystem defaults (LVGL, BLE, PWM, sensor) |

---

## Tests and Linting

**There are no tests.** This is typical for ZMK firmware modules — validation is
done by building and flashing to hardware. There is no `ztest`, `pytest`, or other
test runner configured.

**There is no linting or formatting tooling configured** (no `.clang-format`,
`.editorconfig`, or pre-commit hooks). The Zephyr ecosystem conventionally uses
`clang-format` with a Zephyr-style config. When formatting is needed, follow the
style described below manually.

---

## Code Style

### Language and Headers

- All code is **C11**.
- Use `#pragma once` for all new headers (not `#ifndef` guards). The driver
  override `display_st7789v.h` uses traditional guards — leave it as-is.

### Naming Conventions

| Element | Convention | Example |
|---|---|---|
| Structs | `snake_case`; widgets use `zmk_widget_` prefix | `struct zmk_widget_battery_bar` |
| Struct fields | `snake_case` | `.state_of_charge`, `.obj`, `.node` |
| Public functions | `snake_case`; widgets use `zmk_widget_` prefix | `zmk_widget_battery_bar_init()` |
| Static/internal functions | `snake_case` | `set_battery_bar_value()`, `bl_fade()` |
| Enum values | `UPPER_SNAKE_CASE` | `MOD_TYPE_GUI`, `PERIPHERAL_SLOT_STATE_OPEN` |
| Enum types | `snake_case` | `enum modifier_type` |
| Macros / `#define` | `UPPER_SNAKE_CASE` | `SYMBOL_COMMAND`, `FADE_STEP` |
| Kconfig symbols | `UPPER_SNAKE_CASE` with `PROSPECTOR_` prefix | `CONFIG_PROSPECTOR_SHOW_MODIFIERS` |
| ZMK events | `zmk_` prefix, `_state_changed` or `_changed` suffix | `zmk_caps_word_state_changed` |
| Module-internal namespacing | Short prefix derived from module, e.g. `psptr_` | `psptr_peripheral_slot` |
| Font files | `FontName_Weight_Size.c` | `DINish_Condensed_Semibold_22.c` |

### Formatting

- **Indentation**: 4 spaces (preferred for new code). The driver file uses tabs —
  do not reformat it. Maintain consistency within each file.
- **Braces**: K&R style — opening brace on the same line as control flow.
- **Line length**: No hard limit enforced; keep lines readable (~100 chars).
- **Blank lines**: One blank line between function definitions.

### Types

Use Zephyr/standard fixed-width types throughout:

```c
uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t
bool, true, false   // via <stdbool.h> (included transitively by Zephyr)
```

Key LVGL and Zephyr types:

```c
lv_obj_t *          // LVGL object handle (opaque)
lv_opa_t            // LVGL opacity (uint8_t alias)
lv_color_t          // LVGL color struct
sys_slist_t         // Zephyr singly-linked list head
sys_snode_t         // Zephyr singly-linked list node
zmk_event_t *       // ZMK event pointer
zmk_mod_flags_t     // ZMK modifier bitmask
struct bt_conn *    // Zephyr Bluetooth connection handle
```

Use `float` only when necessary (animation math, ALS brightness calculations).

### Include Order and Style

Use angle brackets for all external includes; quotes for local file-relative headers:

```c
// 1. Zephyr kernel/device headers
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

// 2. ZMK headers
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>

// 3. LVGL
#include <lvgl.h>

// 4. Module-level shared headers (in shield include/)
#include <fonts.h>
#include <symbols.h>
#include <modifier_order.h>

// 5. Local widget headers (relative to current file)
#include "battery_bar.h"
#include "layer_roller.h"
```

### Error Handling

- Return `int`; use Zephyr errno values: `0` success, negative on error (`-EINVAL`,
  `-ENOMEM`, `-EIO`).
- Early return on error:
  ```c
  if (!device_is_ready(dev)) {
      LOG_ERR("Device not ready");
      return -EIO;
  }
  ```
- Guard against null pointers before dereference:
  ```c
  if (!bar || !num) { return; }
  ```
- ZMK event listener callbacks must return `ZMK_EV_EVENT_BUBBLE`.
- Use Zephyr logging macros — never `printf`:
  ```c
  LOG_MODULE_REGISTER(my_module, CONFIG_ZMK_LOG_LEVEL);
  LOG_ERR("..."); LOG_WRN("..."); LOG_INF("..."); LOG_DBG("...");
  ```
  When joining the ZMK log module instead of declaring a new one:
  ```c
  LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);
  ```

---

## ZMK-Specific Patterns

### Custom Events

```c
// include/zmk/events/my_event.h
struct zmk_my_event { bool active; };
ZMK_EVENT_DECLARE(zmk_my_event);  // generates raise_ and as_ helpers

// src/events/my_event.c
#include <zmk/events/my_event.h>
ZMK_EVENT_IMPL(zmk_my_event);
```

Raise with `raise_zmk_my_event(...)`. Consume with `as_zmk_my_event(eh)` (returns
`NULL` if event type doesn't match).

### Display Widget Listener Pattern

Every display widget follows this fixed structure:

```c
// Static list of widget instances
static sys_slist_t widgets;

// State struct
struct widget_state { uint8_t some_value; };

// Extract state from event
static struct widget_state get_state(const zmk_event_t *eh) { ... }

// Update LVGL objects from state
static void update_cb(struct zmk_widget_foo *widget,
                      struct widget_state state) { ... }

// Macro wires everything together
ZMK_DISPLAY_WIDGET_LISTENER(widget_foo, struct widget_state, update_cb, get_state)
ZMK_SUBSCRIPTION(widget_foo, zmk_some_event_type);

// Public init function
int zmk_widget_foo_init(struct zmk_widget_foo *widget, lv_obj_t *parent) {
    sys_slist_append(&widgets, &widget->node);
    widget->obj = lv_obj_create(parent);
    // ... build LVGL object tree ...
    widget_foo_init(widget);  // generated by ZMK_DISPLAY_WIDGET_LISTENER
    return 0;
}

lv_obj_t *zmk_widget_foo_obj(struct zmk_widget_foo *widget) {
    return widget->obj;
}
```

Always declare `sys_snode_t node` as an early field in the widget struct.

### Non-Display Event Listeners

```c
ZMK_LISTENER(my_listener, my_callback);
ZMK_SUBSCRIPTION(my_listener, zmk_event_type);
```

### System Initialization

```c
SYS_INIT(my_init_fn, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
```

### Deferred Work (Animations, Timeouts)

```c
static K_WORK_DELAYABLE_DEFINE(my_work, my_handler);
k_work_reschedule(&my_work, K_SECONDS(3));
k_work_cancel_delayable(&my_work);
```

### DTS Color Theming

New layouts should support the DTS theming system:

```c
#if DT_HAS_CHOSEN(zmk_prospector_theme)
#define THEME_NODE DT_CHOSEN(zmk_prospector_theme)
#else
#define THEME_NODE DT_NODELABEL(prospector_blue_theme)
#endif
#define MY_COLOR DT_PROP(THEME_NODE, my_color_property)
```

Add new color properties to `dts/bindings/zmk,prospector-theme.yaml` and to
all four theme nodes in `boards/shields/prospector_adapter/prospector_adapter.overlay`.

### Driver/Behavior Override Pattern

To replace a ZMK or Zephyr built-in source file, mark the upstream file as
header-only and substitute your version:

```cmake
# In CMakeLists.txt
zephyr_library_amend()
set_source_files_properties(${ZEPHYR_ZMK_MODULE_DIR}/path/to/original.c
    PROPERTIES HEADER_FILE_ONLY ON)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/my_replacement.c)
```

### Layout Dispatch via `#include`

`custom_status_screen.c` selects a layout by `#include`-ing its `status_screen.c`
directly. Each `status_screen.c` is **excluded from the CMakeLists glob** and
compiled only through this include mechanism. Maintain that exclusion filter when
adding new layouts.

```c
#if defined(CONFIG_PROSPECTOR_STATUS_SCREEN_CLASSIC)
#include "layouts/classic/status_screen.c"
#elif defined(CONFIG_PROSPECTOR_STATUS_SCREEN_FIELD)
#include "layouts/field/status_screen.c"
// ...
#endif
```

---

## Adding a New Layout

1. Create `boards/shields/prospector_adapter/src/layouts/<name>/` with:
   - `status_screen.c` (excluded from glob, included by `custom_status_screen.c`)
   - Widget `.c`/`.h` pairs
   - `display_colors.h` (use DTS theming if possible)
   - `Kconfig.defconfig` (select required LVGL components)
2. Add a `CONFIG_PROSPECTOR_STATUS_SCREEN_<NAME>` option in `Kconfig`.
3. Add a glob + regex filter block in the shield `CMakeLists.txt`.
4. Add the `#include` branch in `custom_status_screen.c`.
5. Add font declarations in `include/fonts.h` under the appropriate `#ifdef`.
