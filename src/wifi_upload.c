#include "wifi_upload.h"
#include "upload_name.h"
#include "board_sd.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "upload";

static httpd_handle_t s_httpd;
static bool s_wifi;
static char s_saved[96];
static volatile bool s_have_saved;

static const char PAGE[] = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,viewport-fit=cover\">\n<meta name=\"theme-color\" content=\"#F4F1EA\">\n<title>inkSpeed · Upload</title>\n<style>\n:root{--paper:#F4F1EA;--paper-dark:#E8E2D6;--ink:#141210;--dither-mid:#9C998F;--dither-lt:#D9D4C8;--muted:#6E675E;--focal:#B3261E;\n--mono:\"IBM Plex Mono\",ui-monospace,\"SFMono-Regular\",Menlo,Consolas,monospace;--sans:\"Helvetica Neue\",Arial,\"Liberation Sans\",system-ui,sans-serif}\n*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}\nhtml,body{margin:0;color:var(--ink)}\nbody{font:15px/1.5 var(--mono);background:var(--paper);padding:0 0 48px;\nbackground-image:radial-gradient(var(--dither-lt) 1px,transparent 1px),radial-gradient(var(--dither-lt) 1px,transparent 1px);background-size:4px 4px;background-position:0 0,2px 2px}\n.top{background:rgba(244,241,234,.94);border-bottom:2px solid var(--ink);padding:12px 16px}\n.top>div{max-width:30rem;margin:0 auto;display:flex;align-items:baseline;gap:12px;flex-wrap:wrap}\n.wm{font:800 28px/1 var(--sans);letter-spacing:-.035em;padding:7px 0 5px}\n.wm .f{color:var(--focal);position:relative;display:inline-block}\n.wm .f:before,.wm .f:after{content:\"\";position:absolute;left:50%;width:2px;height:6px;background:var(--ink);transform:translateX(-50%)}\n.wm .f:before{top:-7px}.wm .f:after{bottom:-6px}\n.tag{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--muted)}\n.wrap{max-width:30rem;margin:0 auto;padding:22px 16px}\n.card{background:#fff;border:2px solid var(--ink);border-radius:12px;padding:16px}\n.drop{border:2px dashed var(--ink);border-radius:8px;padding:24px 14px 18px;text-align:center;cursor:pointer;background:var(--paper)}\n.drop.over{background:var(--paper-dark)}\n.drop strong{display:block;font-size:1.02rem}\n.drop small{color:var(--muted)}\n.fname{margin-top:8px;min-height:1.2em;word-break:break-all}\n.row{display:flex;align-items:center;justify-content:space-between;margin:14px 0;padding:10px 2px;border-top:1px solid var(--dither-mid);border-bottom:1px solid var(--dither-mid)}\n.toggle{position:relative;width:42px;height:24px}\n.toggle input{opacity:0;width:0;height:0;position:absolute}\n.track{position:absolute;inset:0;border:2px solid var(--ink);border-radius:999px;background:#fff}\n.track:before{content:\"\";position:absolute;width:14px;height:14px;left:3px;top:3px;border-radius:50%;background:var(--ink);transition:.12s}\n.toggle input:checked+.track{background:var(--ink)}\n.toggle input:checked+.track:before{transform:translateX(18px);background:var(--paper)}\n.toggle input:focus-visible+.track,button:focus-visible{outline:2px solid var(--ink);outline-offset:3px}\nbutton{font:600 14px var(--mono);background:var(--ink);color:var(--paper);border:2px solid var(--ink);border-radius:8px;box-shadow:3px 3px 0 var(--ink);padding:12px 16px;width:100%;cursor:pointer}\nbutton:active{transform:translate(2px,2px);box-shadow:1px 1px 0 var(--ink)}\n#status{min-height:1.4em;margin:12px 2px 0;text-align:center;font-size:13px}\n#status.ok,#status.err{font-weight:700}\n#status.err{color:var(--focal)}\nh2{font-size:12px;letter-spacing:2px;text-transform:uppercase;color:var(--muted);margin:28px 2px 10px;font-weight:600}\nul{list-style:none;margin:0;padding:4px 16px}\nli{display:flex;align-items:center;justify-content:space-between;gap:12px;border-bottom:1px solid var(--dither-mid);padding:10px 0}\nli:last-child{border-bottom:0}\nli span{word-break:break-all}\n.x{width:auto;padding:4px 10px;font-size:12px;background:var(--paper);color:var(--ink);border-radius:8px;box-shadow:2px 2px 0 var(--ink)}\n.x:active{background:var(--ink);color:var(--paper)}\n.empty{color:var(--muted);justify-content:center}\n</style>\n</head>\n<body>\n<header class=\"top\"><div><span class=\"wm\" role=\"img\" aria-label=\"inkSpeed\">ink<span class=\"f\">S</span>peed</span><span class=\"tag\">Wi-Fi upload</span></div></header>\n<main class=\"wrap\">\n<div class=\"card\">\n<div class=\"drop\" id=\"drop\">\n<strong>Drop a file</strong>\n<small>or tap to choose</small>\n<div class=\"fname\" id=\"fname\"></div>\n<input id=\"file\" type=\"file\" hidden>\n</div>\n<div class=\"row\"><span>Prepare for reading</span>\n<label class=\"toggle\"><input id=\"fmt\" type=\"checkbox\" checked aria-label=\"Prepare for reading\"><span class=\"track\"></span></label>\n</div>\n<button id=\"go\" type=\"button\">Upload</button>\n<div id=\"status\" role=\"status\" aria-live=\"polite\"></div>\n</div>\n<h2>On the card</h2>\n<ul id=\"list\" class=\"card\"></ul>\n</main>\n<script>\nconst drop=document.getElementById(\"drop\"), file=document.getElementById(\"file\"),\n      fname=document.getElementById(\"fname\"), status=document.getElementById(\"status\"),\n      list=document.getElementById(\"list\");\ndrop.onclick=()=>file.click();\nfile.onchange=()=>{fname.textContent=file.files[0]?file.files[0].name:\"\";};\n[\"dragenter\",\"dragover\"].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.add(\"over\");}));\n[\"dragleave\",\"drop\"].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.remove(\"over\");}));\ndrop.addEventListener(\"drop\",e=>{\n  const f=e.dataTransfer.files&&e.dataTransfer.files[0]; if(!f) return;\n  const dt=new DataTransfer(); dt.items.add(f); file.files=dt.files; fname.textContent=f.name;\n});\nfunction formatText(s){\n  s=s.replace(/\\r\\n?/g,\"\\n\");\n  s=s.replace(/[\\u00AD]/g,\"\");\n  s=s.replace(/(\\w)-\\n(\\w)/g,\"$1$2\");\n  s=s.replace(/<[^>]+>/g,\" \");\n  s=s.replace(/&nbsp;/g,\" \").replace(/&lt;/g,\"<\").replace(/&gt;/g,\">\").replace(/&quot;/g,'\"').replace(/&#39;/g,\"'\").replace(/&amp;/g,\"&\");\n  s=s.replace(/[\\uFB00]/g,\"ff\").replace(/[\\uFB01]/g,\"fi\").replace(/[\\uFB02]/g,\"fl\");\n  s=s.replace(/[\\u2010-\\u2015]/g,\"-\").replace(/[\\u2018\\u2019]/g,\"'\").replace(/[\\u201C\\u201D]/g,'\"').replace(/[\\u2026]/g,\"...\");\n  s=s.replace(/[\\t ]+/g,\" \");\n  s=s.replace(/\\n{3,}/g,\"\\n\\n\");\n  return s.trim()+\"\\n\";\n}\nasync function loadList(){\n  const res=await fetch(\"/books\"); const text=await res.text();\n  const names=text.split(\"\\n\").map(s=>s.trim()).filter(Boolean);\n  list.innerHTML=\"\";\n  if(!names.length){list.innerHTML='<li class=\"empty\">No books yet</li>';return;}\n  for(const name of names){\n    const li=document.createElement(\"li\");\n    const span=document.createElement(\"span\"); span.textContent=name;\n    const b=document.createElement(\"button\"); b.className=\"x\"; b.type=\"button\"; b.textContent=\"Delete\";\n    b.onclick=async()=>{\n      if(!confirm(\"Delete \"+name+\"?\")) return;\n      status.className=\"\"; status.textContent=\"Deleting…\";\n      const r=await fetch(\"/delete?name=\"+encodeURIComponent(name),{method:\"POST\"});\n      status.className=r.ok?\"ok\":\"err\"; status.textContent=await r.text();\n      loadList();\n    };\n    li.append(span,b); list.append(li);\n  }\n}\ndocument.getElementById(\"go\").onclick=async()=>{\n  const f=file.files[0];\n  if(!f){status.className=\"err\"; status.textContent=\"Choose a file first.\"; return;}\n  status.className=\"\"; status.textContent=\"Reading…\";\n  let name=f.name.split(/[\\\\/]/).pop();\n  let body;\n  try{\n    if(document.getElementById(\"fmt\").checked){\n      status.textContent=\"Formatting…\";\n      body=new TextEncoder().encode(formatText(await f.text()));\n      if(!/\\.txt$/i.test(name)) name=name.replace(/\\.[^.]+$/,\"\")+\".txt\";\n    }else body=await f.arrayBuffer();\n  }catch(e){status.className=\"err\"; status.textContent=\"Could not read that file.\"; return;}\n  status.textContent=\"Uploading…\";\n  try{\n    const res=await fetch(\"/upload?name=\"+encodeURIComponent(name),{method:\"POST\",headers:{\"Content-Type\":\"application/octet-stream\"},body});\n    status.className=res.ok?\"ok\":\"err\"; status.textContent=await res.text();\n  }catch(e){status.className=\"err\"; status.textContent=\"Upload failed.\";}\n  loadList();\n};\nloadList();\n</script>\n</body>\n</html>\n";

static esp_err_t send_page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE, sizeof PAGE - 1);
}

static esp_err_t send_books(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (!board_sd_ready()) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no sd");
    DIR *d = opendir(BOOKS_DIR);
    if (!d) return httpd_resp_send(req, "", 0);
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        size_t n = strlen(de->d_name);
        if (n < 4 || strcasecmp(de->d_name + n - 4, ".txt") != 0) continue;
        httpd_resp_sendstr_chunk(req, de->d_name);
        httpd_resp_sendstr_chunk(req, "\n");
    }
    closedir(d);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t recv_upload(httpd_req_t *req)
{
    if (!board_sd_ready()) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no sd");
    if (req->content_len <= 0 || req->content_len > 8 * 1024 * 1024) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
    }
    char query[256], raw[160], name[96];
    raw[0] = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        httpd_query_key_value(query, "name", raw, sizeof raw);
        upload_url_decode(raw);
    }
    if (!upload_safe_book_name(raw, name, sizeof name)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
    }
    (void)mkdir(BOOKS_DIR, 0777);
    char path[BOOK_PATH_MAX];
    int nw = snprintf(path, sizeof path, "%s/%s", BOOKS_DIR, name);
    if (nw < 0 || (size_t)nw >= sizeof path) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
    }
    FILE *f = fopen(path, "wb");
    if (!f) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "open failed");

    char buf[1024];
    int left = req->content_len;
    bool ok = true;
    while (left > 0) {
        int want = left > (int)sizeof buf ? (int)sizeof buf : left;
        int n = httpd_req_recv(req, buf, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (n <= 0) { ok = false; break; }
        if ((int)fwrite(buf, 1, (size_t)n, f) != n) { ok = false; break; }
        left -= n;
    }
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(path);
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "write failed");
    }
    strncpy(s_saved, name, sizeof s_saved - 1);
    s_saved[sizeof s_saved - 1] = 0;
    s_have_saved = true;
    ESP_LOGI(TAG, "saved %s (%d bytes)", name, req->content_len);
    char msg[128];
    snprintf(msg, sizeof msg, "Saved %s (%d bytes)", name, req->content_len);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_sendstr(req, msg);
}


static esp_err_t recv_delete(httpd_req_t *req)
{
    if (!board_sd_ready()) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no sd");
    char query[256], raw[160], name[96];
    raw[0] = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof query) == ESP_OK) {
        httpd_query_key_value(query, "name", raw, sizeof raw);
        upload_url_decode(raw); /* the page sends encodeURIComponent(name) */
    }
    if (!upload_safe_book_name(raw, name, sizeof name)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
    }
    if (!board_sd_delete_book(name)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "delete failed");
    }
    ESP_LOGI(TAG, "deleted %s", name);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    char msg[128];
    snprintf(msg, sizeof msg, "Deleted %s", name);
    return httpd_resp_sendstr(req, msg);
}

static const httpd_uri_t URI_ROOT = { .uri = "/", .method = HTTP_GET, .handler = send_page };
static const httpd_uri_t URI_BOOKS = { .uri = "/books", .method = HTTP_GET, .handler = send_books };
static const httpd_uri_t URI_UPLOAD = { .uri = "/upload", .method = HTTP_POST, .handler = recv_upload };
static const httpd_uri_t URI_DELETE = { .uri = "/delete", .method = HTTP_POST, .handler = recv_delete };

static esp_err_t start_ap(void)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    if (!esp_netif_get_handle_from_ifkey("WIFI_AP_DEF")) {
        esp_netif_create_default_wifi_ap();
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, WIFI_UPLOAD_SSID, sizeof ap.ap.ssid - 1);
    strncpy((char *)ap.ap.password, WIFI_UPLOAD_PASS, sizeof ap.ap.password - 1);
    ap.ap.ssid_len = strlen(WIFI_UPLOAD_SSID);
    ap.ap.channel = 6;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    s_wifi = true;
    ESP_LOGI(TAG, "AP %s  http://%s", WIFI_UPLOAD_SSID, WIFI_UPLOAD_URL);
    return ESP_OK;
}

esp_err_t wifi_upload_start(void)
{
    if (s_httpd) return ESP_OK;
    esp_err_t err = start_ap();
    if (err != ESP_OK) {
        wifi_upload_stop();
        return err;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    cfg.recv_wait_timeout = 60;
    cfg.send_wait_timeout = 60;
    err = httpd_start(&s_httpd, &cfg);
    if (err != ESP_OK) {
        wifi_upload_stop();
        return err;
    }
    httpd_register_uri_handler(s_httpd, &URI_ROOT);
    httpd_register_uri_handler(s_httpd, &URI_BOOKS);
    httpd_register_uri_handler(s_httpd, &URI_UPLOAD);
    httpd_register_uri_handler(s_httpd, &URI_DELETE);
    return ESP_OK;
}

void wifi_upload_stop(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
    }
    if (s_wifi) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi = false;
    }
}

bool wifi_upload_active(void) { return s_httpd != NULL; }

bool wifi_upload_take_saved(char *name, size_t name_sz)
{
    if (!s_have_saved || !name || !name_sz) return false;
    s_have_saved = false;
    strncpy(name, s_saved, name_sz - 1);
    name[name_sz - 1] = 0;
    return name[0] != 0;
}
