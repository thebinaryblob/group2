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
#define CLOCK_WAIT_UNICAST 2 // Wait befor calling the next neighbor

/* constants */
#define BROADCAST_CHANNEL 128
#define UNICAST_CHANNEL  120
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define CLOCK_WAIT 10 // Wait for reploy when building neighbor table
#define TIMEOUT_WAIT 2 // Timeout for unicast reply
#define PRINT_OUTPUT 5; // Print output if interations % PRINT_OUTPUT = 0

/* Datatype declaration */
struct broadcastMessage {
    int id;
};


struct unicastMessage {
    int senderId;
    int receiverId;
    clock_time_t senderTime;
    clock_time_t receiverTime; // 0 if not set
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
static int randomWait;

// Broadcast
static struct broadcast_conn bcConn;
static const struct broadcast_callbacks broadcastCallback;

// Unicast
// static struct unicastMessage ucRequestForTime, ucReply;
static struct unicast_conn ucConn;
static const struct unicast_callbacks unicastCallback;

/* Function declaration */
static int find_neighbor(int neighborId);
static void add_neighbor(struct neighbor n);
static clock_time_t calc_new_time(clock_time_t senderTime, clock_time_t receiverTimeOld);

// Broadcast
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from);
static void send_broadcast();

// Unicast
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from);

/* Function definition */
/* Return array position of neighbor or -1 */
static int find_neighbor(int neighborId)
{
    static int i;
    for(i = 0; i < neighborArrayOccupied; i++)
    {
        if (neighborTable[i].id == neighborId)
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
static clock_time_t calc_new_time(clock_time_t senderTime, clock_time_t receiverTimeOld)
{
    clock_time_t rtt = clock_time() - receiverTimeOld;
    clock_time_t neighborTime = senderTime + rtt/2;
    clock_time_t result = clock_time() - (clock_time() - neighborTime)/r;
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
    if (find_neighbor(new_neighbor.id) == -1)
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

/* Receive unicast */
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from)
{
        static struct unicastMessage ucMessageReceived;
        packetbuf_copyto(&ucMessageReceived);
        if(ucMessageReceived.isRequestForTime)
        {
            if(debug){printf("Request for time received from %d. Sending reply.\n", ucMessageReceived.senderId);}
            rimeaddr_t addr;
            static struct unicastMessage ucReply;
            ucReply.senderId = node_id;
            ucReply.receiverId = ucMessageReceived.senderId;
            ucReply.senderTime = clock_time();
            ucReply.receiverTime = ucMessageReceived.senderTime; // Return origninal time to calculate rtt
            ucReply.isRequestForTime = 0;

            addr.u8[0] = ucReply.receiverId;
            addr.u8[1] = 0;
            packetbuf_copyfrom(&ucReply, sizeof(ucReply));
            unicast_send(&ucConn, &addr);
            if(debug){printf("Reply sent to node %d.\n", ucReply.receiverId);}

            leds_on(LEDS_GREEN);
            clock_wait(CLOCK_SECOND / 2);
            leds_off(LEDS_GREEN);

        }
        else
        {
            int n = find_neighbor(ucMessageReceived.senderId);
            if(neighborTable[n].answer_expected)
            {
                if(debug){printf("Answer received from %d. Calculating new time.\n", ucMessageReceived.senderId);}
                neighborTable[n].answer_expected = 0; 

                clock_time_t newtime = calc_new_time(ucMessageReceived.senderTime, ucMessageReceived.receiverTime);
                clock_set(newtime);
                if(debug){printf("#### Set time for node %d: %d.####\n", node_id, (uint16_t)clock_time());}
                /* Adjust Timers */
                clock_time_t diff = clock_time - newtime;
                etimer_adjust(&startTimer,diff);
                etimer_adjust(&loopTimer,diff);
                etimer_adjust(&timeoutTimer,diff);
            }
        }

}
static const struct unicast_callbacks unicast_callbacks = {recv_uc};

/*-----------------------------------------------------*/
// Main Process
/*-----------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS_THREAD(main_process, ev, data)
{
    // Gracefully close the communication channels at the end
    PROCESS_EXITHANDLER(broadcast_close(&bcConn);unicast_close(&ucConn);)
    PROCESS_BEGIN();

    broadcast_open(&bcConn, BROADCAST_CHANNEL, &broadcastCallback);
    unicast_open(&ucConn, UNICAST_CHANNEL, &unicast_callbacks);

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

        // Loop over neighbors
        static int i;
        for(i = 0; i < neighborArrayOccupied; i++)
        {
            rimeaddr_t addr;
            static struct unicastMessage ucRequestForTime;
            ucRequestForTime.senderId = node_id;
            ucRequestForTime.receiverId = neighborTable[i].id;
            ucRequestForTime.senderTime = clock_time();
            ucRequestForTime.receiverTime = 0;
            ucRequestForTime.isRequestForTime = 1;
            neighborTable[i].answer_expected = 1;

            addr.u8[0] = ucRequestForTime.receiverId;
            addr.u8[1] = 0;
            packetbuf_copyfrom(&ucRequestForTime, sizeof(ucRequestForTime));
            unicast_send(&ucConn, &addr);
            if(debug){printf("Request for Time sent to node %d.\n", ucRequestForTime.receiverId);}

            leds_on(LEDS_GREEN);
            clock_wait(CLOCK_SECOND / 2);
            leds_off(LEDS_GREEN);

            etimer_set(&timeoutTimer, CLOCK_WAIT*CLOCK_SECOND);
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&loopTimer));
            neighborTable[i].answer_expected = 0;
        }

        // End of Loop
        static int dontPrint = numIter % PRINT_OUTPUT;
        if(!dontPrint)
        {
            printf("LoopTime:%d:%d\n", numIter, (uint16_t)clock_time());
        }
        if(debug){printf("#### End of Loop NR %d####\n", numIter);}
        numIter++;

    }
    PROCESS_END();
}
AUTOSTART_PROCESSES(&main_process);
