import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const ui = await readFile(new URL('../components/hisu_ui/src/hisu_ui.c',import.meta.url),'utf8');
const overlay = ui.split('static void render_idle_status(lv_obj_t *root)')[1].split('static void value_text')[0];
assert.match(overlay,/lv_obj_set_style_bg_opa\(s_idle_clock, LV_OPA_TRANSP, 0\)/);
assert.match(overlay,/lv_obj_set_style_bg_opa\(panel, LV_OPA_TRANSP, 0\)/);
assert.match(overlay,/lv_obj_remove_style_all\(panel\)/);
assert.doesNotMatch(overlay,/lv_obj_set_style_bg_color\((panel|s_idle_clock),/);
assert.doesNotMatch(overlay,/lv_obj_set_style_bg_opa\((panel|s_idle_clock),\s*LV_OPA_COVER/);
assert.match(overlay,/lv_obj_set_style_bg_opa\(s_battery_bars\[i\], LV_OPA_COVER, 0\)/);
assert.match(overlay,/lv_obj_set_style_bg_opa\(s_battery_tip, LV_OPA_COVER, 0\)/);
assert.ok(ui.indexOf('render_idle_status(root);') > ui.indexOf('lv_image_set_src(image,&s_wallpaper_image)'),
  'Transparent status overlay must remain above the wallpaper');
console.log('Idle overlay source checks passed: transparent clock/icon containers, visible battery pixels. Actual LCD rendering not tested.');
