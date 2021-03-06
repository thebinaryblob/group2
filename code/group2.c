/* Group project IoT 2017 */

#include "contiki.h"
#include "net/rime.h" /* for runicast */
#include "dev/button-sensor.h"
#include "dev/leds.h" /* for led control */
#include "dev/light.h"
#include "node-id.h" /* fro node_id */
#include <stdio.h> /* for printing */
#include "sys/rtimer.h" /* for timestamps */


/* Change these variables for thesting */
static clock_time_t r = 2;
static int debug = 0; // Use to toggle debug messages


/* constants */
#define MAX_RETRANSMISSIONS 4
#define BROADCAST_CHANNEL 128
#define RUNICAST_CHANNEL  120
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define CLOCK_WAIT 10 // Wait for reploy when building neighbor table
#define CLOCK_WAIT_RUNICAST 2 // Wait befor calling the next neighbor
#define DEBUG 1;
static int rc_wait_reply = 0; // Wait until we receive a message


/* Datatype declaration */
struct broadcastMessage {
    int id;
    clock_time_t time;
};

struct unicastMessage {
    int id;
    int dest;
    clock_time_t time_sender;
    clock_time_t time_receiver; // 0 if not set
    int answer_expected;
};

struct neighbor {
    int id;
};

struct runicastQueueItem {
    struct unicastMessage msg;
    struct runicastQueueItem *next;
};

/* Variable declaration */
static struct neighbor neighbor_table[ARRAY_SIZE];
static int array_occupied = 0; /* Number of elements in array */
static struct etimer ef, et, er;
static struct etimer leds_off_timer;
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
clock_time_t calc_new_time(clock_time_t t);
// Broadcast
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);
static void send_broadcast();
// Runicast
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);
static void sent_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions);

static struct runicastQueueItem *head = NULL;
static struct runicastQueueItem *current = NULL;

static void createQueue(struct unicastMessage message)
{
    
    if(debug){printf("Creating queue with a message to node %d.\n", message.dest);}
	struct runicastQueueItem *ptr = (struct runicastQueueItem*)malloc(sizeof(struct runicastQueueItem));
	ptr->msg = message;
	ptr->next = NULL;

	head = current = ptr;
}

static void addQueueItem(struct unicastMessage message)
{
    if(NULL == head)
    {
        if(debug){printf("Message for node %d first in queue. Calling create method.\n", message.dest);}
        // printf("beep\n");
    	createQueue(message);
    }
    else
    {
        if(debug){printf("Add message for node %d to queue.\n", message.dest);}
        struct runicastQueueItem *ptr = (struct runicastQueueItem*)malloc(sizeof(struct runicastQueueItem));
        ptr->msg = message;
        ptr->next = NULL;
        current->next = ptr;
        current = ptr;
    }
}

static struct unicastMessage popQueueItem()
{
    struct runicastQueueItem *ptr = NULL;
    ptr = head;
    head = head->next;
    struct unicastMessage res = ptr->msg;
    free(ptr);
    return res;
}

static int queueHasElement()
{
	if(NULL == head)
    {
        // if(debug){printf("Queue has no element.\n");}
    	return 0;
    }
    // if(debug){printf("Queue has elements.\n");}
	return 1;
}

/* Function definition */



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
    // printf("beep\n");
    return -1;
}


/* Add neighbor to array and increasea index */
static void add_neighbor(struct neighbor n, struct neighbor ntb[])
{
    if(debug){printf("Add new neighbor with id %d to array at position %d.\n", n.id, array_occupied);}
    // printf("beep\n");
    ntb[array_occupied] = n;
    array_occupied++;
}

/* Return time difference with neighbor n */
clock_time_t calc_new_time(clock_time_t t)
{
	if(debug){printf("clock_time: %d.\n", clock_time());}
	if(debug){printf("t: %d.\n", t);}
	if(debug){printf("(clock_time-t)/r: %d.\n", (clock_time() - t)/r);}

    clock_time_t result = clock_time() - (clock_time() - t)/r;
    return result;
}

/* Broadcast receiver, builds neighbor table */
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage rsc_msg;
    packetbuf_copyto(&rsc_msg);

    if(debug){printf("#### Receiving Broadcast from node %d ####\n", from->u8[0]);}
    // printf("beep\n");

    leds_on(LEDS_RED);
    clock_wait(CLOCK_SECOND);
    leds_off(LEDS_ALL);

    struct neighbor new_neighbor;
    new_neighbor.id = rsc_msg.id;
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
    clock_wait(CLOCK_SECOND);   	
    leds_off(LEDS_ALL);
}


/* Runicast receiver. Calculate time difference */
static void recv_runicast(struct runicast_conn *c, rimeaddr_t *from, uint8_t seqno)
{
    struct unicastMessage runmsg_received;
    packetbuf_copyto(&runmsg_received);

    leds_on(LEDS_GREEN);
    clock_wait(CLOCK_SECOND);
    leds_off(LEDS_ALL);
    
    if(runmsg_received.answer_expected == 1)
    {
        if(debug){printf("#### Receiving Runicast from node %d. Answer is expected ####\n", from->u8[0]);}
        if(debug){printf("Answering to %d. (Add element to queue.)\n", runmsg_received.id);}
        struct unicastMessage reply_msg;
        reply_msg.id = node_id;
        reply_msg.dest = runmsg_received.id;
        reply_msg.time_sender = runmsg_received.time_sender;
        reply_msg.time_receiver = clock_time();
        reply_msg.answer_expected = 0;
        addQueueItem(reply_msg);
    }
    else
    {
        if(debug){printf("#### Receiving Runicast from node %d. Answer is NOT expected ####\n", from->u8[0]);}

        // rtt
        struct neighbor n;
        n.id = runmsg_received.id;
        int neighbor_positon = find_neighbor(n, neighbor_table);
        clock_time_t rtt = clock_time() - runmsg_received.time_sender;

        if(debug){printf("RTT for neighbor %d is %d.\n", n.id, (uint16_t)rtt);}

        clock_time_t this_neighbor_time = runmsg_received.time_receiver + rtt/2;
        //if(debug){printf("This this_neighbor_time is %d.\n", (uint16_t)this_neighbor_time);}
        //if(debug){printf("Our time is %d.\n", (uint16_t)clock_time());}


        /* Adjust for time drift and update our local time */
        clock_time_t newtime = calc_new_time(this_neighbor_time);
        if(debug){printf("###############################################\n");}
        if(debug){printf("Old time: %d.\n", (uint16_t)clock_time());}
        if(debug){printf("New time: %d.\n", (uint16_t)newtime);}
		
		
		clock_time_t diff = clock_time - newtime
		/* Adjust Timers */
		etimer_adjust(ef,diff);
		etimer_adjust(et,diff);
		etimer_adjust(er,diff);
		etimer_adjust(leds_off_timer,diff);
		
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
    printf("runicast message sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);  
}
static void timedout_runicast(struct runicast_conn *c, rimeaddr_t *to, uint8_t retransmissions)
{
    printf("runicast message timed out when sending to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
    // Could not send. Therefore, we do not expect a reply.
     rc_wait_reply = 0;

}
static const struct runicast_callbacks runicast_callbacks = {recv_runicast, sent_runicast, timedout_runicast};

/*-----------------------------------------------------*/
// Main Process
/*-----------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS_THREAD(main_process, ev, data)
{
    // Gracefully close the communication channels at the end
    PROCESS_EXITHANDLER(broadcast_close(&bc);)
    PROCESS_BEGIN();

    // Open the communication channels to be able to send/receive packets
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

        // Time Adjustment Phase
        // Send Runicast to ever neighbor
        static int i;
        printf("Array occupied is %d.\n", array_occupied);
        for(i = 0; i < array_occupied; i++)
        {


            /* compose and send message */
            struct unicastMessage msg;
            msg.time_sender = clock_time();
            msg.time_receiver = 0;
            msg.answer_expected = 1;
            msg.id = node_id;
            msg.dest = neighbor_table[i].id;
            addQueueItem(msg);
            rc_wait_reply = 1;

            if(rc_wait_reply)
            {
                // Wait for reply until timeout is reached
                if(debug){printf("Awaiting reply from %d.\n", neighbor_table[i].id);}
                etimer_set(&et, CLOCK_WAIT_RUNICAST*CLOCK_SECOND);
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
                rc_wait_reply = 0;
            }
        }

        looper++;
        if(debug){printf("###########################################\n");}
        if(debug){printf("Finished round %d. Waiting and start again.\n", looper);}
        if(debug){printf("###########################################\n");}
        etimer_set(&ef, CLOCK_WAIT*CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&ef));

    }

    PROCESS_END();
}
PROCESS(runicast_sender, "Runicast sender");
PROCESS_THREAD(runicast_sender, ev, data)
{
    // Gracefully close the communication channels at the end
    PROCESS_EXITHANDLER(runicast_close(&runicast);)
    PROCESS_BEGIN();
    // Open the communication channels to be able to send/receive packets
    runicast_open(&runicast, RUNICAST_CHANNEL, &runicast_callbacks);
    // Set timer
    etimer_set(&er, CLOCK_WAIT*CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&er));
    while(1)
    {
        if(queueHasElement() && !runicast_is_transmitting(&runicast))
        {
            struct unicastMessage msg = popQueueItem();
            rimeaddr_t addr;
            addr.u8[0] = msg.dest;
            addr.u8[1] = 0;

            packetbuf_copyfrom(&msg, sizeof(msg));
            runicast_send(&runicast, &addr, MAX_RETRANSMISSIONS);

            /* turn on and of green led */
            leds_on(LEDS_GREEN);
            if(debug){printf("#### Sending Runicast to %d ####\n", (int)addr.u8[0]);}
            // printf("beep\n");
            clock_wait(CLOCK_SECOND);
            leds_off(LEDS_ALL);
        }
      	else
        {
            // if(debug){printf("#### Queque empty or runicast sending. Waiting ... ####\n");}
            // printf("beep\n");
            PROCESS_PAUSE();
        }
      	PROCESS_PAUSE();
    }
    PROCESS_END();
}
AUTOSTART_PROCESSES(&main_process, &runicast_sender);
