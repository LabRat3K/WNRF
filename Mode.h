/*
* Mode.h
*
* Project: WNRF an E1.31 to NRF24L01 bridge
* Copyright (c) 2022 Andrew Williams
* http://www.ratsnest.ca
* Based on ESPixelStick 
* Copyright (c) 2018 Shelby Merrick
* http://www.forkineye.com
*
*  This program is provided free for you to use in any way that you wish,
*  subject to the laws and regulations where you are using it.  Due diligence
*  is strongly suggested before using this code.  Please give credit where due.
*
*  The Author makes no warranty of any kind, express or implied, with regard
*  to this program or the documentation contained in this document.  The
*  Author shall not be liable in any event for incidental or consequential
*  damages in connection with, or arising out of, the furnishing, performance
*  or use of these programs.
*
*/

#ifndef MODE_H_
#define MODE_H_

/* Output Mode - There can be only one! (-Conor MacLeod) */
// Note - for WNRF we have stripped out the MODE_SERIAL and MODE_PIXEL handlers
#define ESPS_MODE_WNRF
#endif  // MODE_H_
