/* BlueCoinT app */
/* Copyright ©2022 - 2023 Adrian Kennard, Andrews & Arnold Ltd.See LICENCE file for details .GPL 3.0 */

static const char TAG[] = "BLE-Env";

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
#include <halib.h>

#ifndef	CONFIG_SOC_BLE_SUPPORTED
#error	You need CONFIG_SOC_BLE_SUPPORTED
#endif

struct
{
   uint8_t ha_send:1;           // Send HA
} b = { 0 };

httpd_handle_t webserver = NULL;

static void
send_ha_config (bleenv_t * d)
{
   d->updated = 0;
   char *tag;
   asprintf (&tag, "/%s", d->name);
   char *id,
    *name;
   asprintf (&id, "temp-%s", d->name);
   asprintf (&name, "Temp %s", d->name);
 ha_config_sensor (id, name: name, stat: tag, field: "temp", type: "temperature", unit: "°C", delete:!ha || !d->tempset ||
                     d->missing);
   free (id);
   free (name);
   asprintf (&id, "humidity-%s", d->name);
   asprintf (&name, "R/H %s", d->name);
 ha_config_sensor (id, name: name, stat: tag, field: "rh", type: "humidity", unit: "%", delete:!ha || !d->humset || d->missing);
   free (id);
   free (name);
   free (tag);
}

const char *
app_callback (int client, const char *prefix, const char *target, const char *suffix, jo_t j)
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
   if (client || !prefix || target || strcmp (prefix, topiccommand) || !suffix)
      return NULL;              //Not for us or not a command from main MQTT
   if (!strcmp (suffix, "connect"))
   {
      lwmqtt_subscribe (revk_mqtt (0), "info/BLE-Env/#");
      b.ha_send = 1;
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
   return revk_web_foot (req, 0, webcontrol >= 2 ? 1 : 0, NULL);
}

/* MAIN */
void
app_main ()
{
   revk_boot (&app_callback);
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
      if (b.ha_send)
      {
         b.ha_send = 0;
         for (bleenv_t * d = bleenv; d; d = d->next)
            send_ha_config (d);
      }
      uint32_t now = uptime ();
      bleenv_expire (missingtime);
      for (bleenv_t * d = bleenv; d; d = d->next)
         if (d->updated)
            send_ha_config (d);
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
            void f (void)
            {
               if (d->tempset)
               {
                  if (d->temp < 0)
                     jo_litf (j, "temp", "-%d.%02d", (-d->temp) / 100, (-d->temp) % 100);
                  else
                     jo_litf (j, "temp", "%d.%02d", d->temp / 100, d->temp % 100);
               }
               if (d->batset)
                  jo_litf (j, "bat", "%d", d->bat);
               if (d->voltset)
                  jo_litf (j, "voltage", "%u.%03u", d->volt / 1000, d->volt % 1000);
               if (d->humset)
                  jo_litf (j, "rh", "%u.%02u", d->hum / 100, d->hum % 100);
            }
            jo_string (j, "address", ble_addr_format (&d->addr));
            jo_string (j, "name", d->name);
            f ();
            jo_int (j, "rssi", d->rssi);
            revk_info ("report", &j);
            ESP_LOGI (TAG, "Report %s \"%s\" %d (%s %d)", ble_addr_format (&d->addr), d->name, d->rssi, d->better, d->betterrssi);
            if (ha)
            {
               j = jo_object_alloc ();
               f ();
               revk_state (d->name, &j);
            }
         }
      bleenv_clean ();
   }
   return;
}
