/* Group project IoT 2017 */

#include "contiki.h"
#include "net/rime.h" /* for runicast */
#include "dev/button-sensor.h"
#include "dev/leds.h" /* for led control */
#include "dev/light.h"
#include "node-id.h" /* fro node_id */
#include <stdio.h> /* for printing */
#include "sys/rtimer.h" /* for timestamps */

/* constants */
#define DEBUG 1 // Use to toggle debug messages
#define MAX_RETRANSMISSIONS 4
#define BROADCAST_CHANNEL 128
#define RUNICAST_CHANNEL  120
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define CLOCK_WAIT 20
static float r = 0.5;


/* Datatype declaration */
struct broadcastMessage {
    int id;
    clock_time_t time;
};

struct unicastMessage {
    int id;
    clock_time_t time;
    int answer_expected;
};

struct neighbor {
    int id;
    clock_time_t this_neighbor_time;
    clock_time_t last_sent; // When did we last call that neighbor? Defaults to 0.
};


/* Variable declaration */
static struct neighbor neighbor_table[ARRAY_SIZE];
static int array_occupied; /* Number of elements in array */
static struct etimer ef;
static struct ctimer leds_off_timer_send;
static struct broadcastMessage tmSent;
// Broadcast
static struct broadcast_conn bc;
static const struct broadcast_callbacks broadcast_callback;
// Runicast
static struct runicast_conn runicast;
static const struct runicast_callbacks runicast_callbacks;

/* Function declaration */
static void timerCallback_turnOffLeds();
static int find_neighbor(struct neighbor n, struct neighbor ntb[]);
static void add_neighbor(struct neighbor n, struct neighbor ntb[]);
clock_time_t calc_new_time(struct neighbor n);
static void print_debug_msg(string msg);
// Broadcast
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);
static void send_broadcast();
// Runicast
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void send_runicast(int node);

/* Function definition */
static void print_debug_msg(string msg)
{
    if(DEBUG)
    {
        printf(msg);
    }
}

/* Timer callback turns off all leds */
static void timerCallback_turnOffLeds()
{
    leds_off(LEDS_ALL);
}


/* Return array position of neighbor or -1 */
static int find_neighbor(struct neighbor n, struct neighbor ntb[])
{
    static int i;
    for(i = 0; i < ARRAY_SIZE; i++)
    {
        if (ntb[i].id == n.id)
        {
            print_debug_msg("Found neighbor at position %d.\n", i);
            return i;
        }
    }
    print_debug_msg("Neighbor not found in array.\n");
    return -1;
}


/* Add neighbor to array and increasea index */
static void add_neighbor(struct neighbor n, struct neighbor ntb[])
{
    print_debug_msg("Add new neighbor with id %d to array at position %d.\n", n.id, array_occupied);
    ntb[array_occupied] = n;
    array_occupied++;
}

/* Return time difference with neighbor n */
clock_time_t calc_new_time(struct neighbor n)
{
    clock_time_t result = clock_time() - r*(clock_time() - n.this_neighbor_time);
    return result;
}

/* Broadcast receiver, builds neighbor table */
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage rsc_msg;
    packetbuf_copyto(&rsc_msg);

    print_debug_msg("#### Receiving Broadcast from node %d ####\n", from->u8[0]);

    leds_on(LEDS_RED);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);

    struct neighbor new_neighbor;
    new_neighbor.id = rsc_msg.id;
    new_neighbor.this_neighbor_time = rsc_msg.time;
    new_neighbor.last_sent = 0;
    if (find_neighbor(new_neighbor, neighbor_table) == -1)
    {
        add_neighbor(new_neighbor, neighbor_table);
    }
}

static const struct broadcast_callbacks broadcast_callback = {recv_bc};

/* Send broadcast */
static void send_broadcast()
{
    /* compose and send message */
    tmSent.time = clock_time();
    tmSent.id = node_id;
    packetbuf_copyfrom(&tmSent, sizeof(tmSent));
    broadcast_send(&bc);

    /* turn on and of blue led */
    leds_on(LEDS_BLUE);
    print_debug_msg("#### Node %d: Sending Broadcast ####\n", node_id);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
}


/* Runicast receiver. Calculate time difference */
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno)
{
    struct unicastMessage runmsg_received;
    packetbuf_copyto(&runmsg_received);

    print_debug_msg("#### Receiving Runicast from node %d ####\n", from->u8[0]);
    leds_on(LEDS_GREEN);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
    if(runmsg_received.answer_expected == 1)
    {
        print_debug_msg("Answering to %d.\n", runmsg_received.id);
        struct unicastMessage reply_msg;
        reply_msg.id = node_id;
        reply_msg.time = clock_time();
        reply_msg.answer_expected = 0;

        rimeaddr_t addr;
        addr.u8[0] = runmsg_received.id;
        addr.u8[1] = 0;

        packetbuf_copyfrom(&reply_msg, sizeof(reply_msg));
        runicast_send(&runicast, &addr, MAX_RETRANSMISSIONS);
    }
    else
    {
        print_debug_msg("No answer required. Computing rtt.\n");
        // rtt
        struct neighbor n;
        n.id = runmsg_received.id;
        int neighbor_positon = find_neighbor(n, neighbor_table);
        clock_time_t rtt = clock_time() - neighbor_table[neighbor_positon].last_sent;
        print_debug_msg("RTT for neighbor %d is %d.\n", n.id, (uint16_t)rtt);

        clock_time_t this_neighbor_time = runmsg_received.time + rtt/2;
        //print_debug_msg("This this_neighbor_time is %d.\n", (uint16_t)this_neighbor_time);
        //print_debug_msg("Our time is %d.\n", (uint16_t)clock_time());

        /* Update neighbor time in array */
        neighbor_table[neighbor_positon].this_neighbor_time = this_neighbor_time;

        /* Adjust for time drift and update our local time */
        clock_time_t newtime = calc_new_time(neighbor_table[neighbor_positon]);
        print_debug_msg("###############################################\n");
        print_debug_msg("Old time: %d.\n", (uint16_t)clock_time());
        print_debug_msg("New time: %d.\n", (uint16_t)newtime);
        clock_set(newtime);
        printf("Set time for node %d: %d.\n", node_id, (uint16_t)clock_time());
        print_debug_msg("###############################################\n");
    }
}

static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    print_debug_msg("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    print_debug_msg("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast, sent_runicast, timedout_runicast};


static void send_runicast(int node)
{

    rimeaddr_t addr;
    addr.u8[0] = neighbor_table[node].id;
    addr.u8[1] = 0;

    /* compose and send message */
    struct unicastMessage msg;
    msg.time = clock_time();
    msg.answer_expected = 1;
    msg.id = node_id;
    packetbuf_copyfrom(&msg, sizeof(msg));
    runicast_send(&runicast, &addr, MAX_RETRANSMISSIONS);

    /* Note when we send to the neighbor */
    neighbor_table[node].last_sent = clock_time();

    /* turn on and of green led */
    leds_on(LEDS_GREEN);
    print_debug_msg("#### Sending Runicast to %d ####\n", (int)addr.u8[0]);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
}

/*-----------------------------------------------------*/
// Main Process
/*-----------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS_THREAD(main_process, ev, data)
{
    // Gracefully close the communication channels at the end
    PROCESS_EXITHANDLER(runicast_close(&runicast); broadcast_close(&bc);)
    PROCESS_BEGIN();

    // Open the communication channels to be able to send/receive packets
    runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
    broadcast_open(&bc, BROADCAST_CHANNEL, &broadcast_callback);

    // Set timer
    etimer_set(&ef, CLOCK_WAIT*CLOCK_SECOND);

    while(1)
    {
        // Neighborhood Discovery Phase
        // Send broadcast and wait for callback
        send_broadcast();
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ef));
        etimer_reset(&ef);

        // Time Adjustment Phase
        // Send Runicast to ever neighbor
        static int i;
        for(i = 0; i < array_occupied; i++)
        {
            /* wait for previouse runicast to finish */
            while(runicast_is_transmitting(&runicast))
            {
                PROCESS_PAUSE();
            }
            send_runicast(i);
        }

        // Wait, then start again
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ef));
        etimer_reset(&ef);

        // Todo: Evaluation
    }

    PROCESS_END();
}
AUTOSTART_PROCESSES(&main_process);
