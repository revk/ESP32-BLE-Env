// ELA BlueCoin stuff

#include "revk.h"
#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "bleenv.h"

static const char TAG[] = "bleenv";

bleenv_t *bleenv = NULL;

bleenv_t *
bleenv_find (ble_addr_t * a, int make)
{                               // Find (create) a device record
   bleenv_t *d;
   for (d = bleenv; d; d = d->next)
      if (d->addr.type == a->type && !memcmp (d->addr.val, a->val, 6))
         break;
   if (!d && !make)
      return d;
   if (!d)
   {
      d = mallocspi (sizeof (*d));
      if (!d)
         return d;
      memset (d, 0, sizeof (*d));
      d->addr = *a;
      d->next = bleenv;
      d->missing = 1;
      bleenv = d;
   }
   d->last = uptime ();
   return d;
}

int
bleenv_gap_disc (struct ble_gap_event *event)
{
   const uint8_t *p = event->disc.data,
      *e = p + event->disc.length_data;
   if (e > p + 31)
      return 0;                 // Silly
   bleenv_t *d = bleenv_find (&event->disc.addr, 0);
   //if (d) ESP_LOG_BUFFER_HEX(event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP ? "Rsp" : "Adv", event->disc.data, event->disc.length_data);
   // Check if a temp device
   const uint8_t *name = NULL;
   const uint8_t *temp = NULL;
   const uint8_t *bat = NULL;
   const uint8_t *volt = NULL;
   const uint8_t *hum = NULL;
   const uint8_t *env = NULL;
   uint16_t man = 0;
   while (p < e)
   {
      const uint8_t *n = p + *p + 1;
      if (n > e)
         break;
      // Type 08/09 are name
      // Type 16 is 16 bit UUID
      if (p[0] > 1 && (p[1] == 8 || p[1] == 9))
         name = p;
      else if (*p == 5 && p[1] == 0x16 && p[2] == 0x6E && p[3] == 0x2A)
         temp = p + 4;          // Temp UUID 2A6E
      else if (*p == 4 && p[1] == 0x16 && p[2] == 0x0F && p[3] == 0x18)
         bat = p + 4;           // Bat UUID 180F
      else if (*p == 4 && p[1] == 0x16 && p[2] == 0x19 && p[3] == 0x2A)
         bat = p + 4;           // Bat UUID 2A16
      else if (*p == 18 && p[1] == 0x16 && p[2] == 0x1A && p[3] == 0x18)
         env = p;               // Env UUID 181A
      else if (*p >= 3 && p[1] == 0x16 && p[2] == 0x1C && p[3] == 0x18)
      {                         // Used for BT Home v1
         const uint8_t *d = p + 4;
         while (d < n)
         {                      // first byte is type(3) and len(5) where type is 0=uint, 1=int, 2=float, 3=string, 4=AC, next byte is meaning
            if (*d == 0x02 && d[1] == 0x01)
               bat = d + 2;
            else if (*d == 0x23 && d[1] == 0x02)
               temp = d + 2;
            else if (*d == 0x03 && d[1] == 0x03)
               hum = d + 2;
            else if (*d == 0x03 && d[1] == 0x0C)
               volt = d + 2;
            d += 1 + (*d & 0x1F);
         }
      }
      if (*p >= 3 && p[1] == 0xFF)
      {                         // Custom type - with manufacturer code
         man = ((p[3] << 8) | p[2]);
         if (man == 0x757)
         {
            if (*p == 5 && p[4] == 0xF1)
               bat = p + 5;
            else if (*p == 6 && p[4] == 0xF2)
               volt = p + 5;
            else if (*p == 6 && p[4] == 0x12)
               temp = p + 5;
         }
      }
      p = n;
   }
   if (!d && !env && man != 0x0757 && !temp)
      return 0;
   char macname[15];
   if (!name)
   {                            // Make a name from MAC
      sprintf (macname, "%c-%02X%02X%02X%02X%02X%02X", 13, event->disc.addr.val[5], event->disc.addr.val[4],
               event->disc.addr.val[3], event->disc.addr.val[2], event->disc.addr.val[1], event->disc.addr.val[0]);
      name = (const uint8_t *) macname;
   }
   if (!d)
      d = bleenv_find (&event->disc.addr, 1);
   if (d->namelen != *name - 1 || memcmp (d->name, name + 2, d->namelen))
   {                            // Update name
      memcpy (d->name, name + 2, d->namelen = *name - 1);
      d->name[d->namelen] = 0;
   }
   if (temp)
   {
      d->temp = ((temp[1] << 8) | temp[0]);     // C*100
      d->tempset = 1;
   }
   if (bat)
   {
      d->bat = *bat;            // percent
      d->batset = 1;
   }
   if (volt)
      d->volt = ((volt[1] << 8) + volt[0]);     // mV
   if (hum)
   {
      d->hum = ((hum[1] << 8) + hum[0]);        // Hum*100
      d->humset = 1;
   }
   if (env)
   {
      if (*env == 18)
      {                         // Extended (custom)
         //ESP_LOG_BUFFER_HEX (event->disc.event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP ? "Rsp" : "Adv", event->disc.data, event->disc.length_data);
         d->temp = ((env[11] << 8) | env[10]);  // C * 100
         d->tempset = 1;
         d->hum = ((env[13] << 8) | env[12]);   // Hum %*100
         d->humset = 1;
         d->volt = ((env[15] << 8) | env[14]);  // mV
         d->voltset = 1;
         d->bat = env[16];      // %
         d->batset = 1;
         // counter
         // flags
         //ESP_LOGE (TAG, "Temp=%d hum=%d volt=%d bat=%d", d->temp, d->hum, d->volt, d->bat);
      }
   }
   d->rssi = event->disc.rssi;
   if (d->missing)
   {
      d->lastreport = 0;
      d->missing = 0;
      d->found = 1;
   }
   ESP_LOGD (TAG, "Temp \"%s\" T%d B%d V%d R%d", d->name, d->temp, d->bat, d->volt, d->rssi);
   return 0;
}

void
bleenv_expire (uint32_t missingtime)
{
   uint32_t now = uptime ();
   // Devices missing
   for (bleenv_t * d = bleenv; d; d = d->next)
      if (!d->missing && d->last + missingtime < now)
      {                         // Missing
         d->missing = 1;
         ESP_LOGI (TAG, "Missing %s %s", ble_addr_format (&d->addr), d->name);
      }
   // Devices found
   for (bleenv_t * d = bleenv; d; d = d->next)
      if (d->found)
      {
         d->found = 0;
         ESP_LOGI (TAG, "Found %s %s", ble_addr_format (&d->addr), d->name);
      }
}

void
bleenv_clean (void)
{
   if (ble_gap_disc_active ())
      return;                   // maybe use a mutex instead
   uint32_t now = uptime ();
   bleenv_t **dd = &bleenv;
   while (*dd)
   {
      bleenv_t *d = *dd;
      if (d->last + 300 < now)
      {
         ESP_LOGD (TAG, "Forget %s %s", ble_addr_format (&d->addr), d->name);
         *dd = d->next;
         free (d);
         continue;
      }
      dd = &d->next;
   }
}

const char *
ble_addr_format (ble_addr_t * a)
{
   static char buf[30];
   snprintf (buf, sizeof (buf), "%02X:%02X:%02X:%02X:%02X:%02X", a->val[5], a->val[4], a->val[3], a->val[2], a->val[1], a->val[0]);
   if (a->type == BLE_ADDR_RANDOM)
      strcat (buf, "(rand)");
   else if (a->type == BLE_ADDR_PUBLIC_ID)
      strcat (buf, "(pubid)");
   else if (a->type == BLE_ADDR_RANDOM_ID)
      strcat (buf, "(randid)");
   //else if (a->type == BLE_ADDR_PUBLIC) strcat(buf, "(pub)");
   return buf;
}

// --------------------------------------------------------------------------------
// Run BLE just for ELA

struct ble_hs_cfg;
struct ble_gatt_register_ctxt;

static int
ble_gap_event (struct ble_gap_event *event, void *arg)
{
   switch (event->type)
   {
   case BLE_GAP_EVENT_DISC:
      {
         bleenv_gap_disc (event);
         break;
      }
   default:
      ESP_LOGD (TAG, "BLE event %d", event->type);
      break;
   }

   return 0;
}

static void
ble_start_disc (void)
{
   struct ble_gap_disc_params disc_params = {
      .passive = 1,
   };
   if (ble_gap_disc (0 /* public */ , BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL))
      ESP_LOGE (TAG, "Discover failed to start");
}

static uint8_t ble_addr_type;
static void
ble_on_sync (void)
{
   ESP_LOGI (TAG, "BLE Discovery Started");
   int rc;

   rc = ble_hs_id_infer_auto (0, &ble_addr_type);
   assert (rc == 0);

   uint8_t addr_val[6] = { 0 };
   rc = ble_hs_id_copy_addr (ble_addr_type, addr_val, NULL);

   ble_start_disc ();
}

static void
ble_on_reset (int reason)
{
}

static void
ble_task (void *param)
{
   ESP_LOGI (TAG, "BLE Host Task Started");
   /* This function will return only when nimble_port_stop() is executed */
   nimble_port_run ();

   nimble_port_freertos_deinit ();
}

void
bleenv_run (void)
{                               // Just run BLE for ELA only
   REVK_ERR_CHECK (esp_wifi_set_ps (WIFI_PS_MIN_MODEM));        /* default mode, but library may have overridden, needed for BLE at same time as wifi */
   nimble_port_init ();

   /* Initialize the NimBLE host configuration */
   ble_hs_cfg.sync_cb = ble_on_sync;
   ble_hs_cfg.reset_cb = ble_on_reset;
   ble_hs_cfg.sm_sc = 1;
   ble_hs_cfg.sm_mitm = 0;
   ble_hs_cfg.sm_bonding = 1;
   ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

   /* Start the task */
   nimble_port_freertos_init (ble_task);
   ESP_LOGI (TAG, "Starting ELA monitoring");
}
#endif
