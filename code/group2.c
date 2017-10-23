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
#define RUNICAST_CHANNEL  120
#define ARRAY_SIZE 40

struct broadcastMessage {
    int id;
    clock_time_t time;
};

struct unicastMessage {
    clock_time_t time;
    int answer_expected;
};

struct neighbor {
    int id;
    clock_time_t time;
};

static struct neighbor neighbor_table[ARRAY_SIZE];
static struct etimer et;
static void timerCallback_turnOffLeds();
static struct ctimer leds_off_timer_send;
static int array_occupied; /* Number of elements in array */

// Return array position of neighbor or -1
static int find_neighbor(struct neighbor n, struct neighbor ntb[])
{
    static int i;
    for(i = 0; i < ARRAY_SIZE; i++)
    {
        if (ntb[i].id == n.id)
        {
            printf("Found neighbor at position %d.\n", i);
            return i;
        }
    }
    printf("Neighbor not found in array.\n");
    return -1;
}

/* Add neighbor to array and increasea index */
static void add_neighbor(struct neighbor n, struct neighbor ntb[])
{
    printf("Add new neighbor with id %d to array at position %d.\n", n.id, array_occupied);
    ntb[array_occupied] = n;
    array_occupied++;
}

/*-----------------------------------------------------*/
// Neighborhood Discovery Phase
/*-----------------------------------------------------*/
static struct broadcastMessage tmSent;
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage rsc_msg;
    printf("Received time %d from %d.%d\n",
            (uint16_t)rsc_msg.time, from->u8[0], from->u8[1]);

    leds_on(LEDS_RED);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);

    struct neighbor new_neighbor;
    new_neighbor.id = rsc_msg.id;
    new_neighbor.time = rsc_msg.time;
    if (find_neighbor(new_neighbor, neighbor_table) == -1)
    {
        add_neighbor(new_neighbor, neighbor_table);
    }

}

/*-----------------------------------------------------*/
static const struct broadcast_callbacks broadcast_callback = {recv_bc};
static struct broadcast_conn bc;
/*-----------------------------------------------------*/
static struct etimer et;
static struct broadcastMessage tmSent;
static void timerCallback_turnOffLeds();
static struct ctimer leds_off_timer_send;
static uint16_t r = 0.5;

/* Timer callback turns off the blue led */
static void timerCallback_turnOffLeds()
{
    leds_off(LEDS_ALL);
}


/*-----------------------------------------------------*/
PROCESS(broadcast_process, "broadcast");
PROCESS(runicast_process, "runicast process");
/*-----------------------------------------------------*/
PROCESS_THREAD(broadcast_process, ev, data)
{

    /* Close broadcast and start unicast when thread ends */
    PROCESS_EXITHANDLER(broadcast_close(&bc);&runicast_process;)
        PROCESS_BEGIN();

    broadcast_open(&bc, BROADCAST_CHANNEL, &broadcast_callback);
    /* Broadcast every 10 seconds */
    etimer_set(&et, 10*CLOCK_SECOND);

    /* Run for n minutes */
    static int i;
    static int f = 3;
    for(i = 0; i < f; i++) {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
        etimer_reset(&et);
        tmSent.time = clock_time();
        tmSent.id = node_id;
        packetbuf_copyfrom(&tmSent, sizeof(tmSent));
        /* send the packet */
        broadcast_send(&bc);

        /* turn on and of blue led */
        leds_on(LEDS_BLUE);
        ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
        printf("sent broadcast message from %d\n", node_id);
        PROCESS_PAUSE();
    }
    PROCESS_END();

}


clock_time_t calc_new_time(struct neighbor n){
    return clock_time() * r * (clock_time() - n.time);
}

/*-----------------------------------------------------*/
// Everything unicast
/*-----------------------------------------------------*/
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno)
{
    printf("Received message from %d.%d\n",
            from->u8[0], from->u8[1]);

    leds_on(LEDS_GREEN);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
}

static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static const struct runicast_callbacks runicast_callbacks = {recv_runicast, sent_runicast, timedout_runicast};
static struct runicast_conn runicast;
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}


PROCESS_THREAD(runicast_process, ev, data)
{
    PROCESS_EXITHANDLER(runicast_close(&runicast);)

        PROCESS_BEGIN();
    runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
    while(1)
    {
        static int i;
        for(i = 0; i < ARRAY_SIZE; i++)
        {
            rimeaddr_t addr;
            addr.u8[0] = neighbor_table[i].id;
            addr.u8[1] = 0;

            struct unicastMessage msg;
            msg.time = clock_time();
            msg.answer_expected = 1;

            printf("Sending runicast to %d.\n", (int)addr.u8[0]);
            packetbuf_copyfrom(&tmSent, sizeof(tmSent));
            runicast_send(&runicast, &addr, MAX_RETRANSMISSIONS);
        }
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
