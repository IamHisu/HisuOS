import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
const sharp = createRequire(import.meta.url)('sharp');
const app = await readFile(new URL('../components/hisu_app/src/hisu_app.c',import.meta.url),'utf8');
const board = await readFile(new URL('../components/hisu_board/src/hisu_board.c',import.meta.url),'utf8');
const ui = await readFile(new URL('../components/hisu_ui/src/hisu_ui.c',import.meta.url),'utf8');
assert.match(board,/esp_register_shutdown_handler\(display_shutdown\)/);
const shutdown = board.split('static void display_shutdown(void)')[1].split('typedef struct')[0];
assert.match(shutdown,/backlight_off\(\)/);
const off = board.split('static esp_err_t backlight_off(void)')[1].split('static void display_shutdown')[0];
assert.ok(off.indexOf('ledc_stop(') < off.indexOf('gpio_reset_pin(LCD_BL)'));
assert.ok(off.indexOf('gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT)') < off.indexOf('gpio_hold_en(LCD_BL)'),
  'Detach PWM and drive GPIO20 low before holding the backlight, matching Muse');
const prepare = board.split('esp_err_t hisu_board_prepare_boot(void)')[1].split('esp_err_t hisu_board_init(')[0];
assert.ok(prepare.indexOf('gpio_set_level(LCD_BL, 0)') < prepare.indexOf('gpio_config(&backlight)'));
assert.ok(prepare.indexOf('gpio_config(&backlight)') < prepare.indexOf('gpio_hold_dis(LCD_BL)'),
  'Reconfigure the backlight low before releasing its restart hold');
assert.ok(prepare.indexOf('gpio_hold_dis(LCD_BL)') < prepare.indexOf('gpio_hold_en(LCD_BL)'),
  'Keep boot backlight held low until the first frame');
assert.match(prepare,/display_reset_hold\(\)/,
  'Keep the LCD reset asserted while services start');
const brightness = board.split('esp_err_t hisu_board_set_brightness(int percent)')[1].split('void hisu_board_enable_volume_hold')[0];
assert.match(brightness,/if \(percent == 0\) return backlight_off\(\)/);
assert.ok(brightness.indexOf('ledc_set_pin(') < brightness.indexOf('gpio_hold_dis(LCD_BL)'));
const defaults = await readFile(new URL('../sdkconfig.defaults',import.meta.url),'utf8');
assert.match(defaults,/CONFIG_ESP_CONSOLE_SECONDARY_NONE=y/);
assert.match(defaults,/CONFIG_USJ_ENABLE_USB_SERIAL_JTAG=n/);
assert.ok(app.indexOf('hisu_board_prepare_boot()') >= 0);
assert.ok(app.indexOf('hisu_board_prepare_boot()') < app.indexOf('hisu_settings_init()'),
  'Backlight must be held off before services start');
const init = board.split('esp_err_t hisu_board_init(')[1].split('esp_err_t hisu_board_display_write(')[0];
assert.ok(init.indexOf('gpio_hold_dis(LCD_RST)') < init.indexOf('esp_lcd_new_panel_st7789'),
  'Release the retained reset only when the panel driver is ready');
assert.match(init,/esp_lcd_panel_disp_on_off\(s_panel, false\)/);
assert.doesNotMatch(init,/esp_lcd_panel_disp_on_off\(s_panel, true\)/);
const hook = await readFile(new URL('../bootloader_components/hisu_boot_hooks/hooks.c',import.meta.url),'utf8');
assert.match(hook,/gpio_ll_set_level\(&GPIO, 20, 0\)/);
assert.match(hook,/gpio_ll_hold_en\(&GPIO, 20\)/);
assert.match(hook,/gpio_ll_set_level\(&GPIO, 45, 0\)/);
const beforeHook = hook.split('void bootloader_before_init(void)')[1].split('void bootloader_after_init(void)')[0];
assert.ok(beforeHook.indexOf('keep_backlight_dark();') < beforeHook.indexOf('esp_rom_delay_us(20000);'));
assert.ok(beforeHook.indexOf('esp_rom_delay_us(20000);') < beforeHook.indexOf('keep_panel_reset();'),
  'Allow the backlight to discharge before the LCD reset turns the retained frame black');
const afterHook = hook.split('void bootloader_after_init(void)')[1];
assert.ok(afterHook.indexOf('keep_backlight_dark();') < afterHook.indexOf('keep_panel_reset();'));
const sequence = ['render_boot_splash();','lv_refr_now(display);','hisu_board_display_show();',
  'hisu_board_set_brightness(boot_brightness);','bool boot_splash = true;'];
let previous = -1;
for (const step of sequence) {
  const position = ui.indexOf(step);
  assert.ok(position > previous,`Boot frame order: ${step}`);
  previous = position;
}
const assets = new URL('../components/hisu_ui/assets/', import.meta.url);
const source = fileURLToPath(new URL('boot_windows_xp_source.gif', assets));
const output = fileURLToPath(new URL('boot_windows_xp.gif', assets));
const before = await sharp(source, {animated:true}).metadata();
const after = await sharp(output, {animated:true}).metadata();
const decoder = await readFile(new URL('../managed_components/lvgl__lvgl/src/libs/gif/AnimatedGIF/src/AnimatedGIF.h', import.meta.url), 'utf8');
const maximum = Number(decoder.match(/#define MAX_WIDTH\s+(\d+)/)[1]);
assert.ok(before.width > maximum, 'Original reproduces decoder width rejection');
assert.ok(after.width <= maximum, 'Prepared asset fits the real decoder limit');
assert.equal(after.width,240); assert.equal(after.pageHeight,240);
assert.equal(after.pages,18); assert.equal(after.pages,before.pages);
assert.equal(after.loop,0); assert.deepEqual(after.delay,before.delay);
const {data,info} = await sharp(output,{animated:true}).ensureAlpha().raw().toBuffer({resolveWithObject:true});
const frameSize = 240 * 240 * info.channels;
assert.equal(info.channels,4); assert.equal(data.length,frameSize * 18);
for (let frame = 0; frame < 18; ++frame) {
  const pixels = data.subarray(frame * frameSize,(frame + 1) * frameSize);
  let lit = 0;
  for (let i = 0; i < pixels.length; i += 4) lit += pixels[i] + pixels[i+1] + pixels[i+2];
  assert.ok(lit > 100000, `Frame ${frame} must not be blank`);
  for (const row of [0,239]) for (let x = 0; x < 240; ++x) {
    const index = (row * 240 + x) * 4;
    const rgb565 = ((pixels[index] >> 3) << 11) | ((pixels[index+1] >> 2) << 5) | (pixels[index+2] >> 3);
    assert.equal(rgb565,0,'Letterbox stays black in the display RGB565 format');
  }
}
assert.notDeepEqual(data.subarray(0,frameSize),data.subarray(frameSize,frameSize*2),'Animation frames differ');
console.log('Boot GIF tests passed: startup/backlight source-order checks, 240x240, 18 nonblank frames, original delays and black letterbox. Actual LCD/DMA not tested.');
