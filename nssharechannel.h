/*
 * nssharechannel.h --
 *
 *      nssharechannel module for AOLServer.
 *
 */

#ifndef __NSSHARECHANNEL__

#define __NSSHARECHANNEL__

#define SHARECHANNEL_VERSION "2.0"
#define SHARECHANNEL_NAME "nssharechannel Module for AOLServer version "
#define SHARECHANNEL_INFO_1 "nssharechannel Development: Core Services (http://www.core-services.fr)."
#define SHARECHANNEL_INFO_2 "nssharechannel Contact: contact@core-services.fr"
#define SHARECHANNEL_INFO_3 "nssharechannel Info: Feel free to send comment, bug reports and improvement ideas !!"


/*
 * Public functions
 */

static int
Ns_AttachTclChannelByName(Tcl_Interp *interp, char *channelName);

static int
Ns_DetachTclChannel(Tcl_Channel channel);

static int
Ns_CloseSharedTclChannel(Tcl_Channel channel);

#endif /* __NSSHARECHANNEL__ */
