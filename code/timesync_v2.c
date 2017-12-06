/* Group project IoT 2017 */

#include "contiki.h"
#include "net/rime.h" /* for unicast */
#include "dev/leds.h" /* for led control */
#include "node-id.h" /* fro node_id */
#include <stdio.h> /* for printing */
#include "sys/rtimer.h" /* for timestamps */
#include "lib/random.h" /* for random numbers */

/* Change these variables for testing */
static uint8_t rMultiplier = 1;
static uint8_t rDevider = 2;
#define CLOCK_WAIT_UNICAST 10 // Wait befor calling the next neighbor
static uint8_t debug = 0; // Use to toggle debug messages
static int dontPrint;

/* constants */
#define BROADCAST_CHANNEL 128
#define UNICAST_CHANNEL  120
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define CLOCK_WAIT 10 // Wait for reploy when building neighbor table
#define PRINT_OUTPUT 1; // Print output if interations % PRINT_OUTPUT = 0
static int numIter = 0; // Number of iterations

/* Datatype declaration */
struct broadcastMessage {
    int id;
};


struct unicastMessage {
    uint8_t senderId;
    uint8_t receiverId;
    clock_time_t senderTime;
    clock_time_t receiverTime; // 0 if not set
    uint8_t isRequestForTime; // True if node has to answer
};

struct neighbor {
    int id;
    clock_time_t offset;
};


/* Variable declaration */
static struct neighbor neighborTable[ARRAY_SIZE];
static int neighborArrayOccupied = 0; /* Number of elements in array */
static struct etimer loopTimer, timeoutTimer;

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
static clock_time_t calc_offset(clock_time_t senderTime, clock_time_t receiverTimeOld);

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
static clock_time_t calc_offset(clock_time_t senderTime, clock_time_t receiverTimeOld)
{
    clock_time_t curr = clock_time();
    clock_time_t rtt = curr - receiverTimeOld;
    clock_time_t neighborTime = senderTime + rtt/2;
    clock_time_t offset = curr - neighborTime;
	if(debug){printf("Result of calc_offset: %d.\n", (int)offset);}
    return offset;
}

/* Broadcast receiver, builds neighbor table */
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from)
{
    static struct broadcastMessage bcReceiveMessage;
    packetbuf_copyto(&bcReceiveMessage);

    if(debug){printf("Receiving Broadcast from node %d.\n", from->u8[0]);}

    /*leds_on(LEDS_RED);
    clock_wait(CLOCK_SECOND / 2);
    leds_off(LEDS_RED);*/

    struct neighbor new_neighbor;
    new_neighbor.id = bcReceiveMessage.id;
    new_neighbor.offset = 0;
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
    /*leds_on(LEDS_BLUE);
    clock_wait(CLOCK_SECOND / 2);
    leds_off(LEDS_BLUE);*/
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

            /*leds_on(LEDS_GREEN);
            clock_wait(CLOCK_SECOND / 2);
            leds_off(LEDS_GREEN);*/

        }
        else
        {
            int n = find_neighbor(ucMessageReceived.senderId);
            
            if(debug){printf("Answer received from %d. Calculating new time.\n", ucMessageReceived.senderId);}

            clock_time_t offset = calc_offset(ucMessageReceived.senderTime, ucMessageReceived.receiverTime);
	    neighborTable[n].offset = offset;
                /*clock_set(newtime);
                if(debug){printf("#### Set time for node %d: %d.####\n", node_id, (uint16_t)clock_time());}
                 Adjust Timers 
                clock_time_t diff = clock_time - newtime;
                etimer_adjust(&startTimer,diff);
                etimer_adjust(&loopTimer,diff);
                etimer_adjust(&timeoutTimer,diff); */
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

    // Scatter Clock Time
    clock_set(-32768 + (random_rand() % 20000));
    //printf("Initial Clock Time:%d\n", (uint16_t) clock_time());

    while(1)
    {
        /*randomWait = (random_rand() % 5);
        if(debug){printf("Random wait is set to %d.\n", randomWait);}
        etimer_set(&startTimer, randomWait*CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&startTimer));*/
	
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

            addr.u8[0] = ucRequestForTime.receiverId;
            addr.u8[1] = 0;
            packetbuf_copyfrom(&ucRequestForTime, sizeof(ucRequestForTime));
            unicast_send(&ucConn, &addr);
            if(debug){printf("Request for Time sent to node %d.\n", ucRequestForTime.receiverId);}

            /*leds_on(LEDS_GREEN);
            clock_wait(CLOCK_SECOND / 2);
            leds_off(LEDS_GREEN);*/
        }

	// wait until all unicast messages are received back
	etimer_set(&timeoutTimer, CLOCK_WAIT_UNICAST*CLOCK_SECOND);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timeoutTimer));

	clock_time_t offset = 0;	
	int offset_count = 0;
	for(i = 0; i < neighborArrayOccupied; i++)
	{
	    clock_time_t offset_tmp = neighborTable[i].offset;
	    neighborTable[i].offset = 0;
	    if(offset_tmp > 0)
	    {
	        offset += offset_tmp;
		offset_count++;
	    }
        }
	
	if(offset_count > 0)
	{
	    //printf("Offset:%d.\n",(uint16_t) offset);
	    clock_time_t newtime = clock_time() - offset * rMultiplier/rDevider;
	    clock_set(newtime);
	    clock_time_t diff = clock_time() - newtime;
            etimer_adjust(&loopTimer,diff);
            etimer_adjust(&timeoutTimer,diff);
        }


        // End of Loop
        dontPrint = numIter % PRINT_OUTPUT;
        if(!dontPrint)
        {
	    printf("LoopTime:%d", numIter);
            printf(":%d",(uint16_t) clock_time());
	    printf(":%d",(uint16_t) offset);
	    printf(":%d\n",(uint16_t) (offset * rMultiplier/rDevider));
        }
        if(debug){printf("#### End of Loop NR %d####\n", numIter);}
        numIter++;

    }
    PROCESS_END();
}
AUTOSTART_PROCESSES(&main_process);
