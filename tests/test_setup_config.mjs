import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';

const root = new URL('../components/hisu_media/src/',import.meta.url);
const ports = await readFile(new URL('hisu_setup_ports.h',root),'utf8');
const media = await readFile(new URL('hisu_media.c',root),'utf8');
const portal = await readFile(new URL('hisu_setup_portal.c',root),'utf8');
const http = Number(ports.match(/#define HISU_SETUP_HTTP_CTRL_PORT\s+(\d+)/)[1]);
assert.ok(http > 1024 && http < 65536);
assert.match(ports,/#define HISU_SETUP_HTTP_PORT 80/);
assert.match(media,/config\.ctrl_port = HISU_SETUP_HTTP_CTRL_PORT;/);
assert.match(media,/config\.server_port = HISU_SETUP_HTTP_PORT;/);
assert.doesNotMatch(media,/httpd_ssl|hisu_media_tls|8443/);
assert.doesNotMatch(portal,/httpd_start|httpd_stop/);
assert.match(portal,/method = HTTP_HEAD/);
assert.match(portal,/method = HTTP_POST, .handler = unknown_post/);
assert.match(portal,/httpd_sess_trigger_close/);
assert.match(portal,/req->method == HTTP_HEAD && !strcmp\(req->uri,"\/"\)/,
  'Canonical HEAD root must not redirect to itself');
assert.match(portal,/hisu_media_http_host_valid\(host,ip\)/);
assert.ok(media.indexOf('hisu_setup_api_register') < media.indexOf('hisu_setup_portal_register'));
const ui = await readFile(new URL('../components/hisu_ui/src/hisu_ui.c',import.meta.url),'utf8');
assert.match(ui,/s_setup_url = media.enabled && network.setup_client_connected;/);
const setupKeys = ui.split('if (s_model.page == UI_SETUP_PAGE) {')[1].split('if (s_model.page == UI_FLASHLIGHT_PAGE)')[0];
assert.match(setupKeys,/hisu_ui_model_key\(&s_model, key\)/);
assert.match(setupKeys,/if \(key == HISU_KEY_BOOT\) \{\s*if \(view == UI_SETUP_STOP\) hisu_media_close\(\);/);
assert.equal((setupKeys.match(/hisu_media_close\(\)/g) || []).length,1,
  'Only confirming the separate Stop option closes Setting; Back must preserve it');
assert.match(ui,/Dừng chế độ Setting\?/);
assert.doesNotMatch(ui,/s_setup_url = !s_setup_url/);
assert.match(media,/hisu_setup_socket_ipv4\(fd,true,&peer\)/);
assert.match(media,/hisu_setup_socket_ipv4\(fd,false,&local\)/);
assert.match(portal,/struct sockaddr_storage storage/);
assert.match(portal,/storage.ss_family == AF_INET6/);
assert.match(portal,/hisu_media_ipv4_mapped\(ipv6->sin6_addr.s6_addr,address\)/);
assert.doesNotMatch(media,/300000000|s_deadline|seconds_left|hisu_media_refresh_window/);
assert.match(media,/s_web_deadline = esp_timer_get_time\(\) \+ 90000000/);
assert.match(media,/s_leave_deadline = esp_timer_get_time\(\) \+ 8000000/);
assert.match(media,/network.setup_client_connected/);
assert.match(media,/uri="\/leave", .method=HTTP_POST, .handler=leave/);
assert.match(media,/!strcmp\(page,expected\) && !s_leave_deadline/);
const api = await readFile(new URL('hisu_setup_api.c',root),'utf8');
assert.doesNotMatch(api,/hisu_media_refresh_window/);
assert.match(ui,/"http:\/\/%s\/"/);
const open = media.split('if (cmd == CMD_OPEN)')[1].split('else if (cmd == CMD_DEFAULT)')[0];
assert.match(open,/hisu_network_setup\(true\)/);
assert.doesNotMatch(open,/network\.connected|hisu_clock|wifi.*join/i,
  'Opening Setting must not depend on station connectivity, credentials or SNTP');
console.log('HTTP Setting source checks passed: shared server, captive routes, URL QR, offline opening. Device behavior not tested.');
