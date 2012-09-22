/*
 * nssharechannel.c --
 *
 *      nssharechannel module for AOLServer.
 *
 */

#include "ns.h"
#include "nssharechannel.h"

/*
 * The Ns_ModuleVersion variable is required.
 */
int Ns_ModuleVersion = 1;

static const char *sharechannel_name = SHARECHANNEL_NAME SHARECHANNEL_VERSION;

/*
 * Global vars to lock and save the shared channels
 */

static Ns_Mutex			gNsShareChannelLock;
static Tcl_HashTable	gNsShareChannelsTable;

/*
 * Private functions
 */
int
Ns_ModuleInit(char *hServer, char *hModule);

static int
nsShareChannelInterpInit(Tcl_Interp *interp, void *context);

static int
ns_DetachChannelCmd(ClientData context, Tcl_Interp *interp, int argc, char **argv);

static int
ns_AttachChannelCmd(ClientData context, Tcl_Interp *interp, int argc, char **argv);

static int
ns_CloseSharedChannelCmd(ClientData context, Tcl_Interp *interp, int argc, char **argv);

static void
nsShareChannelShutdown(void *ctx);

/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *      This is the nssharechannel module's entry point.  AOLserver runs
 *      this function right after the module is loaded.  It is used to
 *      read configuration data, initialize data structures, kick off
 *      the Tcl initialization function (if any), and do other things
 *      at startup.
 *
 * Results:
 *	NS_OK or NS_ERROR
 *
 * Side effects:
 *	Module loads and initializes itself.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ModuleInit(char *hServer, char *hModule)
{
	/*
	 * Log ns_sharechannel Module Init message
	 */
	Ns_Log(Notice, "Loading %s, build on %s/%s", sharechannel_name, __TIME__, __DATE__);
	Ns_Log(Notice, "%s", SHARECHANNEL_INFO_1);
	Ns_Log(Notice, "%s", SHARECHANNEL_INFO_2);
	Ns_Log(Notice, "%s", SHARECHANNEL_INFO_3);

	/*
	 * Initialize the global vars
	 */
	Ns_InitializeMutex(&gNsShareChannelLock);
	Tcl_InitHashTable(&gNsShareChannelsTable, TCL_STRING_KEYS);

    /*
	 * Add the Tcl "ns_*channel" commands to the interpreter pool.
	 */
    Ns_TclInitInterps(hServer, nsShareChannelInterpInit, NULL);

	/*
	 * Add the shutdown procedure to free the memory.
	 */
	 Ns_RegisterShutdown(nsShareChannelShutdown, NULL);

    return NS_OK;
}


static int
ns_SCTest1(ClientData context, Tcl_Interp *interp, int argc, char **argv)
{
	int result = TCL_OK;
	Tcl_Channel channel;

	if (argc != 2)
	{
		Tcl_AppendResult (interp, "wrong # args: should be \"", argv[0], " channelId\"", NULL);
		return TCL_ERROR;
	}
	channel = Tcl_GetChannel(interp, argv[1], NULL);
	if (channel == NULL)
	{
		Tcl_AppendResult (interp, "Error in ", argv[0], ": Cannot find channel named \"", argv[1], "\" in the local TCL Interpreter", NULL);
		result = TCL_ERROR;
	}
	else
	{
		Ns_DetachTclChannel(channel);
	}
    return result;
}
static int
ns_SCTest2(ClientData context, Tcl_Interp *interp, int argc, char **argv)
{
	int result = TCL_OK;

	if (argc != 2)
	{
		Tcl_AppendResult (interp, "wrong # args: should be \"", argv[0], " channelId\"", NULL);
		return TCL_ERROR;
	}
	Ns_AttachTclChannelByName(interp, argv[1]);
    return result;
}
static int
ns_SCTest3(ClientData context, Tcl_Interp *interp, int argc, char **argv)
{
	int result = TCL_OK;
	Tcl_Channel channel;

	if (argc != 2)
	{
		Tcl_AppendResult (interp, "wrong # args: should be \"", argv[0], " channelId\"", NULL);
		return TCL_ERROR;
	}
	channel = Tcl_GetChannel(NULL, argv[1], NULL);
	if (channel == NULL)
	{
		Tcl_AppendResult (interp, "Error in ", argv[0], ": Cannot find channel named \"", argv[1], "\" in the local TCL Interpreter", NULL);
		result = TCL_ERROR;
	}
	else
	{
		Ns_CloseSharedTclChannel(channel);
	}
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * nsShareChannelInterpInit --
 *
 *      Register new commands with the Tcl interpreter.
 *
 * Results:
 *	NS_OK or NS_ERROR
 *
 * Side effects:
 *	A C function is registered with the Tcl interpreter.
 *
 *----------------------------------------------------------------------
 */

static int
nsShareChannelInterpInit(Tcl_Interp *interp, void *context)
{
    Tcl_CreateCommand(interp, "ns_detachchannel", ns_DetachChannelCmd, NULL, NULL);
    Tcl_CreateCommand(interp, "ns_attachchannel", ns_AttachChannelCmd, NULL, NULL);

    Tcl_CreateCommand(interp, "ns_test1", ns_SCTest1, NULL, NULL);
    Tcl_CreateCommand(interp, "ns_test2", ns_SCTest2, NULL, NULL);
    Tcl_CreateCommand(interp, "ns_test3", ns_SCTest3, NULL, NULL);

#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 )
    Tcl_CreateCommand(interp, "ns_closesharedchannel", ns_CloseSharedChannelCmd, NULL, NULL);
#endif /* (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 ) */

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_DetachTclChannel --
 *
 *      A C command that detach a channel from a tcl interpreter.
 *		This channel will stay open when the TCL interp. exits and
 *		can be accessible from another TCL interp using the
 *		"ns_attachchannel" TCL command or "Ns_AttachChannel" C command.
 *
 * Results:
 *	NS_OK is detached / NS_ERROR if problem or already detached
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */

static int
Ns_DetachTclChannel(Tcl_Channel channel)
{
	int result = NS_OK;
	int newFlag;
	Tcl_HashEntry *entry;
	char *channelName;

	Ns_LockMutex(&gNsShareChannelLock);

	if (channel == NULL)
	{
		result = NS_ERROR;
	}
	else
	{
		/*
		 * Get channel name
		 */
		channelName = Tcl_GetChannelName(channel);
		if (channelName == NULL)
		{
			result = NS_ERROR;
		}
		else
		{
			entry = Tcl_CreateHashEntry(&gNsShareChannelsTable, channelName, &newFlag);
			if (newFlag > 0)
			{
				/*
				 * Save the channel into the internal list.
				 */
				Tcl_SetHashValue(entry, channel);
				/*
				 * Register the channel to the NULL interpreter (just increment the refCount)
				 */
				Tcl_RegisterChannel(NULL, channel);
			}
			else
			{
				result = NS_ERROR;
			}
		}
	}

	Ns_UnlockMutex(&gNsShareChannelLock);

    return result;
}



/*
 *----------------------------------------------------------------------
 *
 * ns_DetachChannelCmd --
 *
 *      A Tcl command that detach a channel from a tcl interpreter.
 *		This channel will stay open when the TCL interp. exits and
 *		can be accessible from another TCL interp using the
 *		"ns_attachchannel" command.
 *
 * Results:
 *	NS_OK
 *
 * Side effects:
 *	Tcl result is set to a string value.
 *
 *----------------------------------------------------------------------
 */

static int
ns_DetachChannelCmd(ClientData context, Tcl_Interp *interp, int argc, char **argv)
{
	int result = TCL_OK;
	int newFlag;
	Tcl_Channel channel;
	Tcl_HashEntry *entry;

	/*
	 * Checking number of args
	 */
	if (argc != 2)
	{
		Tcl_AppendResult (interp, "wrong # args: should be \"", argv[0], " channelId\"", NULL);
		return TCL_ERROR;
	}

	Ns_LockMutex(&gNsShareChannelLock);


	/*
	 * Find the channel from the channelname
	 */
	channel = Tcl_GetChannel(interp, argv[1], NULL);
	if (channel == NULL)
	{
		Tcl_AppendResult (interp, "Error in ", argv[0], ": Cannot find channel named \"", argv[1], "\" in the local TCL Interpreter", NULL);
		result = TCL_ERROR;
	}
	else
	{
		/*
		 * Check if the channel not already registered
		 */
		entry = Tcl_CreateHashEntry(&gNsShareChannelsTable, argv[1], &newFlag);
		if (newFlag > 0)
		{
			/*
			 * Save the channel into the internal list.
			 */
			Tcl_SetHashValue(entry, channel);
			/*
			 * Register the channel to the NULL interpreter (just increment the refCount)
			 */
			Tcl_RegisterChannel(NULL, channel);
		}
		else
		{
			Tcl_AppendResult (interp, "Error in ", argv[0], ": Channel ", argv[1], " already detached.", NULL);
			result = TCL_ERROR;
		}
	}

	Ns_UnlockMutex(&gNsShareChannelLock);

    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * ns_AttachChannelCmd --
 *
 *      A C command that attach a channel opened from another TCL interpreter
 *		to the TCL interp passed in param.
 *
 * Results:
 *	NS_OK is attached / NS_ERROR if problem
 *
 * Side effects:
 *
 *
 *----------------------------------------------------------------------
 */

static int
Ns_AttachTclChannelByName(Tcl_Interp *interp, char *channelName)
{
	int result = NS_OK;
	Tcl_HashEntry *entry;
	Tcl_Channel channel;

	Ns_LockMutex(&gNsShareChannelLock);

	/*
	 * Find the channel from the channelname
	 */
	entry = Tcl_FindHashEntry(&gNsShareChannelsTable, channelName);
	if (entry == NULL)
	{
		result =  NS_ERROR;
	}
	else
	{
		/*
		 * Check if value is not NULL
		 */
		channel = (Tcl_Channel)Tcl_GetHashValue(entry);
		if (channel == NULL)
		{
			result = NS_ERROR;
		}
		else
		{
			/*
			 * Register the channel to the actual interp, so the channel is accessible from TCL code
			 */
			Tcl_RegisterChannel(interp, channel);
		}
	}

	Ns_UnlockMutex(&gNsShareChannelLock);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_AttachChannelCmd --
 *
 *      A Tcl command that attach a channel opened from another TCL interpreter
 *		to the actual TCL interp.
 *
 * Results:
 *	NS_OK
 *
 * Side effects:
 *	Tcl result is set to a string value.
 *
 *----------------------------------------------------------------------
 */

static int
ns_AttachChannelCmd(ClientData context, Tcl_Interp *interp, int argc, char **argv)
{
	int result = TCL_OK;
	Tcl_HashEntry *entry;
	Tcl_Channel channel;

	/*
	 * Checking number of args
	 */
	if (argc != 2)
	{
		Tcl_AppendResult (interp, "Wrong # args: should be \"", argv[0], " channelId\"", NULL);
		return TCL_ERROR;
	}

	Ns_LockMutex(&gNsShareChannelLock);

	/*
	 * Find the channel from the channelname
	 */
	entry = Tcl_FindHashEntry(&gNsShareChannelsTable, argv[1]);
	if (entry == NULL)
	{
		Tcl_AppendResult (interp, "Error in ", argv[0], ": Cannot find shared channel named \"", argv[1], "\"", NULL);
		result =  TCL_ERROR;
	}
	else
	{
		/*
		 * Check if value is not NULL
		 */
		channel = (Tcl_Channel)Tcl_GetHashValue(entry);
		if (channel == NULL)
		{
			Tcl_AppendResult (interp, "Error in ", argv[0], ": corrupted entry for channel \"", argv[1], "\"", NULL);
			result =  TCL_ERROR;
		}
		else
		{
			/*
			 * Register the channel to the actual interp, so the channel is accessible from TCL code
			 */
			Tcl_RegisterChannel(interp, channel);
		}
	}

	Ns_UnlockMutex(&gNsShareChannelLock);

    return result;
}

#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 )
/*
 *----------------------------------------------------------------------
 *
 * ns_CloseSharedTclChannel --
 *
 *      A C command that detach a shared channel opened in the "global
 *		interp" (that is the NULL interp)
 *
 * Results:
 *	NS_OK if detached / NS_ERROR if problem
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
Ns_CloseSharedTclChannel(Tcl_Channel channel)
{
	int result = TCL_OK;
	Tcl_HashEntry *entry;
	char *channelName;

	Ns_LockMutex(&gNsShareChannelLock);

	/*
	 * Find the channel Name
	 */
	channelName = Tcl_GetChannelName(channel);
	if (channelName == NULL)
	{
		result = NS_ERROR;
	}
	else
	{
		/*
		 * Find the channel from the channelname
		 */
		entry = Tcl_FindHashEntry(&gNsShareChannelsTable, channelName);
		if (entry == NULL)
		{
			result =  NS_ERROR;
		}
		else
		{
			/*
			 * Check if value is not NULL
			 */
			channel = (Tcl_Channel)Tcl_GetHashValue(entry);
			if (channel == NULL)
			{
				result =  NS_ERROR;
			}
			else
			{
				/*
				 * Delete the channel from the HashTable
				 */
				Tcl_DeleteHashEntry(entry);
				/*
				 * UnRegister the channel to the NULL interp
				 */
				Tcl_UnregisterChannel(NULL, channel);
			}
		}
	}

	Ns_UnlockMutex(&gNsShareChannelLock);

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_CloseSharedChannelCmd --
 *
 *      A Tcl command that detach a shared channel opened in the "global
 *		interp" (that is the NULL interp)
 *
 * Results:
 *	NS_OK if detached / NS_ERROR if problem
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
ns_CloseSharedChannelCmd(ClientData context, Tcl_Interp *interp, int argc, char **argv)
{
	int result = TCL_OK;
	Tcl_HashEntry *entry;
	Tcl_Channel channel;

	/*
	 * Checking number of args
	 */
	if (argc != 2)
	{
		return NS_ERROR;
	}

	Ns_LockMutex(&gNsShareChannelLock);

	/*
	 * Find the channel from the channelname
	 */
	entry = Tcl_FindHashEntry(&gNsShareChannelsTable, argv[1]);
	if (entry == NULL)
	{
		result =  NS_ERROR;
	}
	else
	{
		/*
		 * Check if value is not NULL
		 */
		channel = (Tcl_Channel)Tcl_GetHashValue(entry);
		if (channel == NULL)
		{
			result =  NS_ERROR;
		}
		else
		{
			/*
			 * Delete the channel from the HashTable
			 */
			Tcl_DeleteHashEntry(entry);
			/*
			 * UnRegister the channel to the NULL interp
			 */
			Tcl_UnregisterChannel(NULL, channel);
		}
	}

	Ns_UnlockMutex(&gNsShareChannelLock);

    return result;
}

#endif /* (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 ) */

/*
 * nsShareChannelShutdown free the memory
 */
static void
nsShareChannelShutdown(void *ctx)
{
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 )
	/*
	 * Close all shared channels remaining opened
	 */
	Tcl_HashEntry *entry;
	Tcl_HashSearch search;
	Tcl_Channel channel;

	for (entry = Tcl_FirstHashEntry(&gNsShareChannelsTable, &search) ; entry != NULL ; entry = Tcl_NextHashEntry(&search) )
	{
		if (entry != NULL)
		{
			channel = (Tcl_Channel)Tcl_GetHashValue(entry);
			Ns_CloseSharedTclChannel(channel);
		}
	}
#endif /* (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION >= 4 ) */

	Tcl_DeleteHashTable(&gNsShareChannelsTable);
	Ns_DestroyMutex(&gNsShareChannelLock);
}
