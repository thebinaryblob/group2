/* Group project IoT 2017 */

#include "contiki.h"
#include "net/rime.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "node-id.h"
#include <stdio.h>

#define MAX_RETRANSMISSIONS 4

/*-----------------------------------------------------*/
// Neighborhood Discovery Phase
/*-----------------------------------------------------*/

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
