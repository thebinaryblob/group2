/* Group project IoT 2017 */

#include "contiki.h"
#include "net/rime.h"
#include "dev/button-sensor.h"
#include "dev/leds.h" /* for led control */
#include "dev/light.h"
#include "node-id.h" /* fro node_id */
#include <stdio.h>
#include "sys/rtimer.h" /* for timestamps */

#define MAX_RETRANSMISSIONS 4
#define BROADCAST_CHANNEL 128

struct broadcastMessage {
    clock_time_t time;
	unsigned short originator;
};

struct neighbor {
    int id;
    clock_time_t time;
};

static struct etimer et;
static struct broadcastMessage tmSent;
static void timerCallback_turnOffLeds();
static struct ctimer leds_off_timer_send;

/*-----------------------------------------------------*/
// Neighborhood Discovery Phase
/*-----------------------------------------------------*/
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage rsc_msg;
    packetbuf_copyto(&rsc_msg);

    printf("Received time %d from %d.%d\n",
            (uint16_t)rsc_msg.time, from->u8[0], from->u8[1]);

    leds_on(LEDS_RED);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
}

/*-----------------------------------------------------*/
static const struct broadcast_callbacks broadcast_callback = {recv_bc};
static struct broadcast_conn bc;
/*-----------------------------------------------------*/

/* Timer callback turns off the blue led */
static void timerCallback_turnOffLeds()
{
  leds_off(LEDS_ALL);
}


/*-----------------------------------------------------*/
PROCESS(broadcast_process, "broadcast");
/*-----------------------------------------------------*/
PROCESS_THREAD(broadcast_process, ev, data)
{

  PROCESS_EXITHANDLER(broadcast_close(&bc);)
  PROCESS_BEGIN();

  broadcast_open(&bc, BROADCAST_CHANNEL, &broadcast_callback);
  /* Broadcast every 10 seconds */
  etimer_set(&et, 10*CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    etimer_reset(&et);
    tmSent.time = clock_time();
    tmSent.originator = node_id;
    packetbuf_copyfrom(&tmSent, sizeof(tmSent));
    /* send the packet */
    broadcast_send(&bc);

    /* turn on and of blue led */
    leds_on(LEDS_BLUE);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
    printf("sent broadcast message from %d\n", node_id);
  }
  PROCESS_END();

}


AUTOSTART_PROCESSES(&broadcast_process);
// Send broadcast with timestamp x
// Receive unicast with timestamp x
// Compare x to local time y => Set up neighbours
// Reply to broadcast with uncast and my ID
// Repeat every n minus

/*-----------------------------------------------------*/
// Convergence Phase
/*-----------------------------------------------------*/

// Loop over neighbours
// Adjust local time accordingly
// Implement algorithms from page 8

/*-----------------------------------------------------*/
// Evaluation
/*-----------------------------------------------------*/
