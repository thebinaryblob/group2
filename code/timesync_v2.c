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
#define CLOCK_WAIT 10 // Wait for reploy when building neighbor table
#define CLOCK_WAIT_UNICAST 10 // Wait before converging
static int dontPrint;

/* constants */
#define BROADCAST_CHANNEL 128
#define UNICAST_CHANNEL  120
#define ARRAY_SIZE 40 // Numer of nodes in cluster
#define PRINT_OUTPUT 1; // Print output if interations % PRINT_OUTPUT = 0
static int numIter = 0; // Number of iterations

/* Datatype declaration */
struct broadcastMessage {
	int id;
};

struct unicastMessage {
	uint8_t senderId;
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
static void send_uc(uint8_t receiverId, clock_time_t receiverTime, uint8_t isRequestForTime);

// Converging
static void converge();

/* Function definition */
/* Return array position of neighbor or -1 */
static int find_neighbor(int neighborId) {
	static int i;
	for (i = 0; i < neighborArrayOccupied; i++) {
		if (neighborTable[i].id == neighborId) {
			return i;
		}
	}
	return -1;
}

/* Add neighbor to array and increasea index */
static void add_neighbor(struct neighbor n) {
	neighborTable[neighborArrayOccupied] = n;
	neighborArrayOccupied++;
}

/* Return time difference with neighbor n */
static clock_time_t calc_offset(clock_time_t senderTime,
	clock_time_t receiverTimeOld) {
	clock_time_t curr = clock_time();
	clock_time_t rtt = curr - receiverTimeOld;
	clock_time_t neighborTime = senderTime + rtt / 2;
	clock_time_t offset = curr - neighborTime;
	return offset;
}

/* Broadcast receiver, builds neighbor table */
static void recv_bc(struct broadcast_conn *c, rimeaddr_t *from) {
	static struct broadcastMessage bcReceiveMessage;
	packetbuf_copyto(&bcReceiveMessage);
	struct neighbor new_neighbor;
	new_neighbor.id = bcReceiveMessage.id;
	new_neighbor.offset = 0;
	if (find_neighbor(new_neighbor.id) == -1) {
		add_neighbor(new_neighbor);
	}
}

static const struct broadcast_callbacks broadcastCallback = { recv_bc };

/* Send broadcast */
static void send_broadcast() {
	static struct broadcastMessage bcSendMessage;
	bcSendMessage.id = node_id;
	packetbuf_copyfrom(&bcSendMessage, sizeof(bcSendMessage));
	broadcast_send(&bcConn);
}

/* Receive unicast */
static void recv_uc(struct unicast_conn *c, const rimeaddr_t *from) {
	static struct unicastMessage ucMessageReceived;
	packetbuf_copyto(&ucMessageReceived);
	if (ucMessageReceived.isRequestForTime) {
		send_uc(ucMessageReceived.senderId,ucMessageReceived.senderTime,0);
	} else {
		int n = find_neighbor(ucMessageReceived.senderId);
		clock_time_t offset = calc_offset(ucMessageReceived.senderTime,ucMessageReceived.receiverTime);
		neighborTable[n].offset = offset;
	}

}

static void send_uc(uint8_t receiverId, clock_time_t receiverTime, uint8_t isRequestForTime) {
	rimeaddr_t addr;
	static struct unicastMessage ucReply;
	ucReply.senderId = node_id;
	ucReply.senderTime = clock_time();
	ucReply.receiverTime = receiverTime;
	ucReply.isRequestForTime = isRequestForTime;

	addr.u8[0] = receiverId;
	addr.u8[1] = 0;
	packetbuf_copyfrom(&ucReply, sizeof(ucReply));
	unicast_send(&ucConn, &addr);
}
static const struct unicast_callbacks unicast_callbacks = { recv_uc };

static void converge(int numIter)
{
	clock_time_t offset = 0;
	static int i;
	for (i = 0; i < neighborArrayOccupied; i++) {
		offset += neighborTable[i].offset;
		neighborTable[i].offset = 0;
	}

	clock_time_t newtime = clock_time() - (int) offset * rMultiplier / rDevider;
	clock_set(newtime);


	// Print for charts
	dontPrint = numIter % PRINT_OUTPUT;
	if (!dontPrint) {
		printf("LoopTime:%d", numIter);
		printf(":%d", (uint16_t) clock_time());
		printf(":%d", (uint16_t) offset);
		printf(":%d\n", (uint16_t) ((int) offset * rMultiplier / rDevider));
	}
}

/*-----------------------------------------------------*/
// Main Process
/*-----------------------------------------------------*/
PROCESS(main_process, "Main process");
PROCESS_THREAD( main_process, ev, data) {
	// Gracefully close the communication channels at the end
	PROCESS_EXITHANDLER(broadcast_close(&bcConn);unicast_close(&ucConn);)
	PROCESS_BEGIN();

	broadcast_open(&bcConn, BROADCAST_CHANNEL, &broadcastCallback);
	unicast_open(&ucConn, UNICAST_CHANNEL, &unicast_callbacks);

	// Scatter Clock Time
	clock_set(-32768 + (random_rand() % 20000));

	while (1) {

		// Send broadcast and wait for replies
		send_broadcast();
		etimer_set(&loopTimer, CLOCK_WAIT * CLOCK_SECOND);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&loopTimer));

		// Loop over neighbors
		static int i;
		for (i = 0; i < neighborArrayOccupied; i++) {
			send_uc(neighborTable[i].id,0,1);
		}

		// wait until all unicast messages are received back
		etimer_set(&timeoutTimer, CLOCK_WAIT_UNICAST * CLOCK_SECOND);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&timeoutTimer));

		converge(numIter++);
	}

	PROCESS_END();
}

AUTOSTART_PROCESSES(&main_process);
