/* BlueCoinT app */
/* Copyright ©2022 - 2023 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static __attribute__((unused))
     const char TAG[] = "BLE-Env";

#include "revk.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"
#include "esp_http_server.h"
#include "esp_crt_bundle.h"
#include "esp_bt.h"
#include "host/util/util.h"
#include "console/console.h"
#include <driver/gpio.h>
#include "bleenv.h"

#ifndef	CONFIG_SOC_BLE_SUPPORTED
#error	You need CONFIG_SOC_BLE_SUPPORTED
#endif

#define	MAXGPIO	36
#define BITFIELDS "-"
#define PORT_INV 0x40
#define port_mask(p) ((p)&0x3F)

     httpd_handle_t webserver = NULL;

#define	settings		\
	u8(webcontrol,2)        \
	u32(missingtime,30)	\
	u32(reporting,60)	\
	u8(temprise,50)		\

#define u32(n,d)        uint32_t n;
#define s8(n,d) int8_t n;
#define u8(n,d) uint8_t n;
#define b(n) uint8_t n;
#define s(n) char * n;
#define io(n,d)           uint8_t n;
settings
#undef io
#undef u32
#undef s8
#undef u8
#undef b
#undef s
     const char *app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
{
   if (j && target && !strcmp (prefix, "info") && !strcmp (suffix, "report") && strlen (target) <= 12)
   {                            // Other reports
      ble_addr_t a = {.type = BLE_ADDR_PUBLIC };
      if (jo_find (j, "rssi"))
      {
         int rssi = jo_read_int (j);
         if (jo_find (j, "address"))
         {
            uint8_t add[18] = { 0 };
            jo_strncpy (j, add, sizeof (add));
            for (int i = 0; i < 6; i++)
               a.val[5 - i] =
                  (((isalpha (add[i * 3]) ? 9 : 0) + (add[i * 3] & 0xF)) << 4) + (isalpha (add[i * 3 + 1]) ? 9 : 0) +
                  (add[i * 3 + 1] & 0xF);
            bleenv_t *d = bleenv_find (&a, 0);
            if (d)
            {
               int c = strcmp (target, d->better);
               if (!c || !*d->better || rssi > d->rssi || (rssi == d->rssi && c > 0))
               {                // Record best
                  if (c)
                  {
                     strcpy (d->better, target);
                     ESP_LOGI (TAG, "Found possibly better \"%s\" %s %d", d->name, target, rssi);
                  }
               }
               d->betterrssi = rssi;
               d->lastbetter = uptime ();
            }
         }
      }
   }
   if (client || !prefix || target || strcmp (prefix, prefixcommand) || !suffix)
      return NULL;              //Not for us or not a command from main MQTT
   if (!strcmp (suffix, "connect"))
   {
      lwmqtt_subscribe (revk_mqtt (0), "info/BLE-Env/#");
   }
   if (!strcmp (suffix, "shutdown"))
      httpd_stop (webserver);
   return NULL;
}

static void
register_uri (const httpd_uri_t * uri_struct)
{
   esp_err_t res = httpd_register_uri_handler (webserver, uri_struct);
   if (res != ESP_OK)
   {
      ESP_LOGE (TAG, "Failed to register %s, error code %d", uri_struct->uri, res);
   }
}

static void
register_get_uri (const char *uri, esp_err_t (*handler) (httpd_req_t * r))
{
   httpd_uri_t uri_struct = {
      .uri = uri,
      .method = HTTP_GET,
      .handler = handler,
   };

   register_uri (&uri_struct);
}

static void
web_head (httpd_req_t * req, const char *title)
{
   revk_web_head (req, title);
}

static esp_err_t
web_root (httpd_req_t * req)
{
   // webcontrol=0 means no web
   // webcontrol=1 means user settings, not wifi settings
   // webcontrol=2 means all
   if (revk_link_down () && webcontrol >= 2)
      return revk_web_settings (req);   // Direct to web set up
   web_head (req, hostname == revk_id ? appname : hostname);
   // Nothing here
   return revk_web_foot (req, 0, webcontrol >= 2 ? 1 : 0);
}

/* MAIN */
void
app_main ()
{
   revk_boot (&app_callback);
#define io(n,d)           revk_register(#n,0,sizeof(n),&n,"- "#d,SETTING_SET|SETTING_BITFIELD);
#define b(n) revk_register(#n,0,sizeof(n),&n,NULL,SETTING_BOOLEAN);
#define u32(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s8(n,d) revk_register(#n,0,sizeof(n),&n,#d,SETTING_SIGNED);
#define u8(n,d) revk_register(#n,0,sizeof(n),&n,#d,0);
#define s(n) revk_register(#n,0,0,&n,NULL,0);
   settings
#undef io
#undef u32
#undef s8
#undef u8
#undef b
#undef s
      revk_start ();

   revk_wait_mqtt (60);

   if (webcontrol)
   {
      // Web interface
      httpd_config_t config = HTTPD_DEFAULT_CONFIG ();
      // When updating the code below, make sure this is enough
      // Note that we're also adding revk's own web config handlers
      config.max_uri_handlers = 2 + revk_num_web_handlers ();
      if (!httpd_start (&webserver, &config))
      {
         if (webcontrol >= 2)
            revk_web_settings_add (webserver);
         register_get_uri ("/", web_root);
         //register_get_uri ("/apple-touch-icon.png", web_icon);
         // When adding, update config.max_uri_handlers
      }
   }


   bleenv_run ();

   /* main loop */
   while (1)
   {
      usleep (100000);
      uint32_t now = uptime ();
      bleenv_expire (missingtime);
      for (bleenv_t * d = bleenv; d; d = d->next)
         if (*d->better && d->lastbetter + reporting * 3 / 2 < now)
            *d->better = 0;     // Not seeing better
      // Reporting
      for (bleenv_t * d = bleenv; d; d = d->next)
         if (!d->missing && (d->lastreport + reporting <= now || d->tempreport + temprise < d->temp))
         {
            d->lastreport = now;
            d->tempreport = d->temp;
            if (*d->better && (d->betterrssi > d->rssi || (d->betterrssi == d->rssi && strcmp (d->better, revk_id) > 0)))
            {
               ESP_LOGI (TAG, "Not reporting \"%s\" %d as better %s %d", d->name, d->rssi, d->better, d->betterrssi);
               continue;
            }
            jo_t j = jo_object_alloc ();
            jo_string (j, "address", ble_addr_format (&d->addr));
            jo_string (j, "name", d->name);
            if (d->temp < 0)
               jo_litf (j, "temp", "-%d.%02d", (-d->temp) / 100, (-d->temp) % 100);
            else
               jo_litf (j, "temp", "%d.%02d", d->temp / 100, d->temp % 100);
            if (d->bat)
               jo_litf (j, "bat", "%d", d->bat);
            if (d->volt)
               jo_litf (j, "voltage", "%u.%03u", d->volt / 1000, d->volt % 1000);
            if (d->hum)
               jo_litf (j, "rh", "%u.%02u", d->hum / 100, d->hum % 100);
            jo_int (j, "rssi", d->rssi);
            revk_info ("report", &j);
            ESP_LOGI (TAG, "Report %s \"%s\" %d (%s %d)", ble_addr_format (&d->addr), d->name, d->rssi, d->better, d->betterrssi);
         }

      bleenv_clean ();
   }
   return;
}
