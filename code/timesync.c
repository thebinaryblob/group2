/* Group project IoT 2017 */

#include "contiki.h"
#include "net/rime.h" /* for unicast */
#include "dev/leds.h" /* for led control */
#include "node-id.h" /* fro node_id */
#include <stdio.h> /* for printing */
#include "sys/rtimer.h" /* for timestamps */
#include "lib/random.h" /* for random numbers */

/* Change these variables for thesting */
static clock_time_t r = 2;
static int debug = 0; // Use to toggle debug messages
static int numIter = 0; // Number of iterations

/* constants */
#define BROADCAST_CHANNEL 128
#define UNICAST_CHANNEL  120
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define CLOCK_WAIT 10 // Wait for reploy when building neighbor table
#define CLOCK_WAIT_UNICAST 2 // Wait befor calling the next neighbor

/* Datatype declaration */
struct broadcastMessage {
    int id;
};


struct unicastMessage {
    int senderId;
    int receiverId;
    clock_time_t time_sender;
    clock_time_t time_receiver; // 0 if not set
    int isRequestForTime; // True if node has to answer
};

struct neighbor {
    int id;
    int answer_expected;
};


/* Variable declaration */
static struct neighbor neighborTable[ARRAY_SIZE];
static int neighborArrayOccupied = 0; /* Number of elements in array */
static struct etimer startTimer, loopTimer, timeoutTimer;
static struct etimer ledTimer;
static int randomWait;

// Broadcast
// static struct broadcastMessage bcSendMessage, bcReceiveMessage;
static struct broadcast_conn bcConn;
static const struct broadcast_callbacks broadcastCallback;

// Unicast
// static struct unicastMessage ucRequestForTime, ucReplyForTime;
static struct unicast_conn ucConn;
static const struct unicast_callbacks unicastCallback;

/* Function declaration */
static int find_neighbor(struct neighbor n);
static void add_neighbor(struct neighbor n);
static clock_time_t calc_new_time(clock_time_t t);

// Broadcast
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);
static void send_broadcast();

// Unicast
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from);

/* Function definition */
/* Return array position of neighbor or -1 */
static int find_neighbor(struct neighbor n)
{
    static int i;
    for(i = 0; i < neighborArrayOccupied; i++)
    {
        if (neighborTable[i].id == n.id)
        {
            if(debug){printf("Found neighbor at position %d.\n", i);}
            return i;
        }
    }
    if(debug){printf("Neighbor not found in array.\n");}
    return -1;
}

/* Add neighbor to array and increasea index */
static void add_neighbor(struct neighbor n)
{
    if(debug){printf("Add new neighbor with id %d to array at position %d.\n", n.id, neighborArrayOccupied);}
    neighborTable[neighborArrayOccupied] = n;
    neighborArrayOccupied++;
}


/* Return time difference with neighbor n */
static clock_time_t calc_new_time(clock_time_t t)
{
    clock_time_t result = clock_time() - (clock_time() - t)/r;
	if(debug){printf("Result of calc_new_time: %d.\n", (int)result);}
    return result;
}

/* Broadcast receiver, builds neighbor table */
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage bcReceiveMessage;
    packetbuf_copyto(&bcReceiveMessage);

    if(debug){printf("Receiving Broadcast from node %d.\n", from->u8[0]);}

    leds_on(LEDS_RED);
    clock_wait(CLOCK_SECOND / 2);
    leds_off(LEDS_RED);

    struct neighbor new_neighbor;
    new_neighbor.id = bcReceiveMessage.id;
    new_neighbor.answer_expected = 0;
    if (find_neighbor(new_neighbor) == -1)
    {
        add_neighbor(new_neighbor);
    }
}

static const struct broadcast_callbacks broadcastCallback = {recv_bc};

/* Send broadcast */
static void send_broadcast()
{
    static struct broadcastMessage bcSendMessage;
    bcSendMessage.id = node_id;
    packetbuf_copyfrom(&bcSendMessage, sizeof(bcSendMessage));
    broadcast_send(&bcConn);

    if(debug){printf("Node %d: Sending Broadcast.\n", node_id);}
    leds_on(LEDS_BLUE);
    clock_wait(CLOCK_SECOND / 2);
    leds_off(LEDS_BLUE);
}

// TODO: Unicast


/*-----------------------------------------------------*/
// Main Process
/*-----------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS_THREAD(main_process, ev, data)
{
    // Gracefully close the communication channels at the end
    PROCESS_EXITHANDLER(broadcast_close(&bcConn);)
    PROCESS_BEGIN();

    broadcast_open(&bcConn, BROADCAST_CHANNEL, &broadcastCallback);

    while(1)
    {
        randomWait = (random_rand() % 5);
        if(debug){printf("Random wait is set to %d.\n", randomWait);}
        etimer_set(&startTimer, randomWait*CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&startTimer));

        // Send broadcast and wait for replies
        send_broadcast();
        etimer_set(&loopTimer, CLOCK_WAIT*CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&loopTimer));

        // End of Loop
        if(debug){printf("#### End of Loop ####\n");}
        printf("Iteration %d finished\n", numIter);
        numIter++;


    }
    PROCESS_END();
}

AUTOSTART_PROCESSES(&main_process);







