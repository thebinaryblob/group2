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
#define MAX_RETRANSMISSIONS 4
#define BROADCAST_CHANNEL 128
#define RUNICAST_CHANNEL  120
#define ARRAY_SIZE 40
static uint16_t r = 0.5;


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
    clock_time_t last_sent; // Is 0 if not used
};


/* Variable declaration */
static struct neighbor neighbor_table[ARRAY_SIZE];
static struct etimer et, ef;
static struct ctimer leds_off_timer_send;
static int array_occupied; /* Number of elements in array */
static struct broadcastMessage tmSent;
static struct broadcast_conn bc;
static void timerCallback_turnOffLeds();

/* Function declaration */
static void timerCallback_turnOffLeds();
static int find_neighbor(struct neighbor n, struct neighbor ntb[]);
// Broadcast
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);
static const struct broadcast_callbacks broadcast_callback = {recv_bc};
// Runicast
static struct runicast_conn runicast;
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static const struct runicast_callbacks runicast_callbacks = {recv_runicast, sent_runicast, timedout_runicast};

/* Function definition */

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

clock_time_t calc_new_time(struct neighbor n)
{
    clock_time_t result = clock_time() - r*(clock_time() - n.this_neighbor_time);
    return result;
}

static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage rsc_msg;
    packetbuf_copyto(&rsc_msg);

    printf("Broadcast: Received time %d from %d.%d\n",
            (uint16_t)rsc_msg.time, from->u8[0], from->u8[1]);

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

static void build_neighbor_table()
{

    printf("################################\n");
    printf("Phase 1 started.\n");
    printf("################################\n");

    /* Broadcast every 10 seconds */
    etimer_set(&et, 10*CLOCK_SECOND);

    /* Run for n times */
    static int i;
    static int f = 2;
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
        printf("Sent broadcast message from our own id: %d\n", node_id);
    }
    /* Wait for potential messages to come in. */
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
    etimer_reset(&et);
    printf("################################\n");
    printf("Phase 1 completed.\n");
    printf("################################\n");
}


static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno)
{
    struct unicastMessage runmsg_received;
    packetbuf_copyto(&runmsg_received);

    printf("RUNICAST: Received message from %d.%d\n",
            from->u8[0], from->u8[1]);

    leds_on(LEDS_GREEN);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
    if(runmsg_received.answer_expected == 1)
    {
        // runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);

        printf("Answering to %d.\n", runmsg_received.id);
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
        printf("No answer required. Computing rtt.\n");
        // rtt 
        struct neighbor n;
        n.id = runmsg_received.id;
        int neighbor_positon = find_neighbor(n, neighbor_table);
        clock_time_t rtt = clock_time() - neighbor_table[neighbor_positon].last_sent;
        printf("RTT for neighbor %d is %d.\n", n.id, (uint16_t)rtt);

        clock_time_t this_neighbor_time = runmsg_received.time + rtt/2;
        printf("This this_neighbor_time is %d.\n", (uint16_t)this_neighbor_time);
        printf("Our time is %d.\n", (uint16_t)clock_time());

        /* Update neighbor time in array */
        neighbor_table[neighbor_positon].this_neighbor_time = this_neighbor_time;

        /* Adjust for time drift and update our local time */
        clock_time_t newtime = calc_new_time(neighbor_table[neighbor_positon]);
        printf("###############################################\n");
        printf("Old time: %d.\n", (uint16_t)clock_time());
        printf("New time: %d.\n", (uint16_t)newtime);
        clock_set_seconds(newtime);
        printf("Set time: %d.\n", (uint16_t)clock_time());
        printf("###############################################\n");
    }
}

static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}


contact_neighbors()
{
    etimer_set(&ef, 20*CLOCK_SECOND);

    printf("################################\n");
    printf("Start sending runicast messages.\n");
    printf("################################\n");
    while(1)
    {
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ef));
        etimer_reset(&ef);
        static int i;
        for(i = 0; i < array_occupied; i++)
        {
            runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
            rimeaddr_t addr;
            addr.u8[0] = neighbor_table[i].id;
            addr.u8[1] = 0;

            struct unicastMessage msg;
            msg.time = clock_time();
            msg.answer_expected = 1;
            msg.id = node_id;

            /* Note when we send to the neighbor */
            neighbor_table[i].last_sent = clock_time();

            printf("Sending runicast to %d.\n", (int)addr.u8[0]);
            packetbuf_copyfrom(&msg, sizeof(msg));
            runicast_send(&runicast, &addr, MAX_RETRANSMISSIONS);

        }
    }
}


/*-----------------------------------------------------*/
// Evaluation
/*-----------------------------------------------------*/

/*-----------------------------------------------------*/
// Main Process
/*-----------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS_THREAD(main_process, ev, data)
{
    // Gracefully close the communication channels at the end
    PROCESS_EXITHANDLER(runicast_close(&runicast); broadcast_close(&bc);)
    PROCESS_BEGIN()
    // Open the communication channels to be able to send/receive packets
    runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
    broadcast_open(&bc, BROADCAST_CHANNEL, &broadcast_callback);
    // Neighborhood Discovery Phase with broadcast
    build_neighbor_table();
    // Time adjustment using runicast
    contact_neighbors();

    PROCESS_END()
}
AUTOSTART_PROCESSES(&main_process);
