/* Group project IoT 2017 */
// TODO:
// - RTT can be negativ because we use time.now() befor and after resetting our clock time.
// - Iterations stop after 3-5 turns when 3+ nodes are used. Debug.
// - Print output to tarwis to get information about our experiments

#include "contiki.h"
#include "net/rime.h" /* for runicast */
#include "dev/button-sensor.h"
#include "dev/leds.h" /* for led control */
#include "dev/light.h"
#include "node-id.h" /* fro node_id */
#include <stdio.h> /* for printing */
#include "sys/rtimer.h" /* for timestamps */
#include "packetqueue.h"

/* constants */
#define MAX_RETRANSMISSIONS 4
#define BROADCAST_CHANNEL 128
#define RUNICAST_CHANNEL  120
#define PACKETQUEUE	(queue , 20 * sizeof(struct packet))
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define CLOCK_WAIT 10
static float r = 0.5;
static int debug = 1; // Use to toggle debug messages
static int rc_wait_reply = 0; // Wait until we receive a message


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

struct packet {
	struct unicastMessage msg;
	rimeaddr_t addr;
	int node;
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
// Broadcast
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);
static void send_broadcast();
// Runicast
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void send_runicast(int node);
static void enqueue_packet(struct unicastMessage msg, rimeaddr_t addr, int node);

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
            if(debug){printf("Found neighbor at position %d.\n", i);}
            return i;
        }
    }
    if(debug){printf("Neighbor not found in array.\n");}
    return -1;
}


/* Add neighbor to array and increasea index */
static void add_neighbor(struct neighbor n, struct neighbor ntb[])
{
    if(debug){printf("Add new neighbor with id %d to array at position %d.\n", n.id, array_occupied);}
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

    if(debug){printf("#### Receiving Broadcast from node %d ####\n", from->u8[0]);}

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
    if(debug){printf("#### Node %d: Sending Broadcast ####\n", node_id);}
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);
}


/* Runicast receiver. Calculate time difference */
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno)
{
    struct unicastMessage runmsg_received;
    packetbuf_copyto(&runmsg_received);

    if(debug){printf("#### Receiving Runicast from node %d ####\n", from->u8[0]);}
    leds_on(LEDS_GREEN);
    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);

    // Wait for other runicasts to finish
    while(runicast_is_transmitting(&runicast))
    {
        //if(debug){printf("Runicast busy sending to %d. Waiting.\n", neighbor_table[i].id);}
        //PROCESS_PAUSE();
    }

    if(runmsg_received.answer_expected == 1)
    {
        if(debug){printf("Answering to %d.\n", runmsg_received.id);}
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
        if(debug){printf("No answer required. Computing rtt.\n");}
        // rtt
        struct neighbor n;
        n.id = runmsg_received.id;
        int neighbor_positon = find_neighbor(n, neighbor_table);
        clock_time_t rtt = clock_time() - neighbor_table[neighbor_positon].last_sent;

        // rtt can be negativ (bug) lets make sure it is positive for now
        if((uint16_t)rtt < 0)
        {
            rtt = 1;
        }
        if(debug){printf("RTT for neighbor %d is %d.\n", n.id, (uint16_t)rtt);}

        clock_time_t this_neighbor_time = runmsg_received.time + rtt/2;
        //if(debug){printf("This this_neighbor_time is %d.\n", (uint16_t)this_neighbor_time);}
        //if(debug){printf("Our time is %d.\n", (uint16_t)clock_time());}

        /* Update neighbor time in array */
        neighbor_table[neighbor_positon].this_neighbor_time = this_neighbor_time;

        /* Adjust for time drift and update our local time */
        clock_time_t newtime = calc_new_time(neighbor_table[neighbor_positon]);
        if(debug){printf("###############################################\n");}
        if(debug){printf("Old time: %d.\n", (uint16_t)clock_time());}
        if(debug){printf("New time: %d.\n", (uint16_t)newtime);}
        clock_set(newtime);
        // Inform the etimer library that the system clock has changed
        etimer_request_poll();
        printf("Set time for node %d: %d.\n", node_id, (uint16_t)clock_time());
        if(debug){printf("###############################################\n");}
        rc_wait_reply = 0;
        if(debug){printf("Received reply. Ready to send next runicast.\n");}
    }
}

static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    if(debug){printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);}
}
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    if(debug){printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);}
}

static const struct runicast_callbacks runicast_callbacks = {recv_runicast, sent_runicast, timedout_runicast};


static void send_runicast(int node)
{

    rc_wait_reply = 1;
    rimeaddr_t addr;
    addr.u8[0] = neighbor_table[node].id;
    addr.u8[1] = 0;

    /* compose and send message */
    struct unicastMessage msg;
    msg.time = clock_time();
    msg.answer_expected = 1;
    msg.id = node_id;

    enqueue_packet(msg, addr, node);
}


static void enqueue_packet(struct unicastMessage msg, rimeaddr_t addr, int node)
{
	static struct packet packet;
	packet.msg = msg;
	packet.addr = addr;
	packet.node = node;
	// 0 lifetime means it stays in the queue indefenetly
	packetqueue_enqueue_packetbuf(&queue,0,&packet);
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

    static int looper = 0;

    while(1)
    {
        // Neighborhood Discovery Phase
        // Send broadcast and wait for callback
        send_broadcast();
        // Set timer
        etimer_set(&ef, CLOCK_WAIT*CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ef));
        etimer_reset(&ef);

        // Time Adjustment Phase
        // Send Runicast to ever neighbor
        static int i;
        for(i = 0; i < array_occupied; i++)
        {

            send_runicast(i);

            /* wait for previouse runicast to finish */
            while(runicast_is_transmitting(&runicast))
            {
                if(debug){printf("Runicast busy sending to %d. Waiting.\n", neighbor_table[i].id);}
                PROCESS_PAUSE();
            }

            while(rc_wait_reply)
            {
                if(debug){printf("Awaiting reply from %d.\n", neighbor_table[i].id);}
                PROCESS_PAUSE();
            }

        }

        if(debug){printf("Finished round %d. Waiting and start again.\n", looper++);}
        // PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ef));
        // etimer_reset(&ef);

        // Todo: Evaluation
    }

    PROCESS_END();
}

PROCESS(runicast_sender, "Runicast Sender");
PROCESS_THREAD(runicast_sender, ev, data)
{
    PROCESS_BEGIN()
    // init packetqueue
	packetqueue_init(&queue);

	while(1){
	    //wait for previouse runicast to finish and for packets to be enqueued
	    while(runicast_is_transmitting(&runicast) && packetqueue_len(&queue) < 1)
	    {
	    	//if(debug){printf("Runicast busy sending to %d. Waiting.\n", neighbor_table[i].id);}
	    	PROCESS_PAUSE();
	    }

	    struct packet packet;
	    packet = *(struct packet*)packetqueue_ptr(packetqueue_first(&queue));
	    // Remove packet from queue
	    packetqueue_dequeue(&queue);

	    packetbuf_copyfrom(&packet.msg, sizeof(packet.msg));
	    runicast_send(&runicast, &packet.addr, MAX_RETRANSMISSIONS);

	    /* Note when we send to the neighbor */
	    neighbor_table[packet.node].last_sent = clock_time();

	    /* turn on and of green led */
	    leds_on(LEDS_GREEN);
	    if(debug){printf("#### Sending Runicast to %d ####\n", (int)packet.addr.u8[0]);}
	    ctimer_set(&leds_off_timer_send, CLOCK_SECOND, timerCallback_turnOffLeds, NULL);

	    PROCESS_PAUSE();
	}

    PROCESS_END();
 }

AUTOSTART_PROCESSES(&main_process, &runicast_sender);
