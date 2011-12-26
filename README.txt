For some variety I occassionally install FreeBSD. However, I couldn’t find 
any UPS control software that fit my needs. So I studied the sources of 
various Linux centric UPS daemons such as genpowerd and powerd. TinyUPS 
Daemon is what I call the result of my efforts.

An Uninterruptible Power Supply is required. Any UPS supporting dumb 
signalling should work but it has only been tested with an APC Back-UPS 
and the cable shown.

/*
 * UPS Side                           Serial Port Side
 * 9 Pin Male                         9 Pin Female
 *
 * Shutdown UPS 1 <---------------> 3 TX  (high = kill power)
 * Line Fail    2 <---------------> 1 DCD (high = power fail)
 * Ground       4 <---------------> 5 GND
 * Low Battery  5 <----+----------> 6 DSR (low = low battery)
 *                     `---|  |---> 4 DTR (cable power)
 */

