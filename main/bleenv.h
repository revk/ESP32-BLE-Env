// ELA Blue Coin stuff

#ifdef	CONFIG_BT_NIMBLE_ENABLED
#define	ELA
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"

typedef struct bleenv_s bleenv_t;
struct bleenv_s
{
   bleenv_t *next;              // Linked list
   ble_addr_t addr;             // Address (includes type)
   uint8_t namelen;             // Device name length
   char name[32];               // Device name (null terminated)
   char better[13];             // ID (Mac) of better device
   int8_t betterrssi;           // Better RSSI
   int8_t rssi;                 // RSSI
   uint32_t lastbetter;         // uptime when last better entry
   uint32_t last;               // uptime of last seen
   uint32_t lastreport;         // uptime of last reported
   int16_t temp;                // Temp *100
   int16_t tempreport;          // Temp last reported
   uint16_t volt;               // Bat voltage
   uint16_t hum;                // Hum %*100
   int8_t bat;                  // Bat %
   uint8_t updated:1;		// found/missing updated (for user to clear when they have handled)
   uint8_t found:1;
   uint8_t missing:1;
   uint8_t tempset:1;           // If fields set
   uint8_t humset:1;
   uint8_t batset:1;
   uint8_t voltset:1;
};
extern bleenv_t *bleenv;

const char *ble_addr_format (ble_addr_t * a);
bleenv_t *bleenv_find (ble_addr_t * a, int make);       // Find a device by address
int bleenv_gap_disc (struct ble_gap_event *event);      // Handle GAP disc event
void bleenv_expire (uint32_t missingtime);      // Expire (i.e. missing)
void bleenv_clean (void);       // Delete old entries

void bleenv_run (void);         // Run BLE for ELA
#endif
