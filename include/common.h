
#pragma once

// clang-format off

/*
  MSDN says, basically, that the maximum length of a path is 260 characters,
  which is represented by the constant MAX_PATH.  Except when it isn't.

  The maximum length of a directory path is MAX_PATH - 12 because it must be
  possible to create a file in 8.3 format under any valid directory.

  Unicode versions of filesystem API functions accept paths up to 32767
  characters if the first four (wide) characters are L"\\?\" and each component
  of the path, separated by L"\", does not exceed the value of
  lpMaximumComponentLength returned by GetVolumeInformation(), which is
  probably 255.  But might not be.

  Relative paths are always limited to MAX_PATH because the L"\\?\" prefix
  is not valid for a relative path.

  Note that we don't care about the last two paragraphs because we're only
  concerned with allocating buffers big enough to store valid paths.  If the
  user tries to store invalid paths they will fit in the buffers but the
  application will fail.  The reason for the failure will end up in the
  event log and the user will realise the mistake.

  So that's that cleared up, then.
*/

enum class nssmconst : uint16_t
{
	pathlength					= INT16_MAX,		// Maximum path length for NSSM - PATH_LENGTH
	dirlength					= pathlength - 12,	// Maximum directory length for NSSM - DIR_LENGTH
};

// Methods used to try to stop the application
enum class stopmethod : uint8_t
{				
	none						= UINT8_MAX,
	console						= 1 << 0,	// NSSM_STOP_METHOD_CONSOLE
	window						= 1 << 1,	// NSSM_STOP_METHOD_WINDOW
	threads						= 1 << 2,	// NSSM_STOP_METHOD_THREADS
	terminate					= 1 << 3	// NSSM_STOP_METHOD_TERMINATE
};

// Startup types
enum class startup : uint8_t
{
	automatic					= 0,		// NSSM_STARTUP_AUTOMATIC
	delayed						= 1,		// NSSM_STARTUP_DELAYED
	manual						= 2,		// NSSM_STARTUP_MANUAL
	disabled					= 3 		// NSSM_STARTUP_DISABLED
};

// Exit actions
enum class exit : uint8_t
{
	restart						= 0,		// NSSM_EXIT_RESTART
	ignore						= 1,		// NSSM_EXIT_IGNORE
	really						= 2,		// NSSM_EXIT_REALLY
	unclean						= 3,		// NSSM_EXIT_UNCLEAN
	numactions					= 4,		// NSSM_NUM_EXIT_ACTIONS
};

// Process priority
enum class priority : uint8_t
{
	realtime					= 0,		// NSSM_REALTIME_PRIORITY
	high						= 1,		// NSSM_HIGH_PRIORITY
	abovenormal					= 2,		// NSSM_ABOVE_NORMAL_PRIORITY
	normal						= 3,		// NSSM_NORMAL_PRIORITY
	belownormal					= 4,		// NSSM_BELOW_NORMAL_PRIORITY
	idle						= 5,		// NSSM_IDLE_PRIORITY
};

enum class wait : uint32_t
{
	reset_throttle_restart		= 1500,		// Throttle the restart of the service if it stops before this many milliseconds have elapsed since startup. Override in registry. - NSSM_RESET_THROTTLE_RESTART
	kill_console_grace_period	= 1500,		// How many milliseconds to wait for the application to die after sending a Control-C event to its console. Override in registry. - NSSM_KILL_CONSOLE_GRACE_PERIOD
	kill_window_grace_period	= 1500,		// How many milliseconds to wait for the application to die after posting to its windows' message queues. Override in registry. - NSSM_KILL_WINDOW_GRACE_PERIOD
	kill_threads_grace_period	= 1500,		// How many milliseconds to wait for the application to die after posting to its threads' message queues. Override in registry. - NSSM_KILL_THREADS_GRACE_PERIOD
	rotatedelay					= 0,		// How many milliseconds to pause after rotating logs - NSSM_ROTATE_DELAY
	waithintmargin				= 2000,		// Margin of error for service status wait hints in milliseconds - NSSM_WAITHINT_MARGIN
	statusdeadline				= 20000,	// How many milliseconds to wait before updating service status - NSSM_SERVICE_STATUS_DEADLINE
	controlstart				= 0,		// User-defined service controls can be in the range 128-255 - NSSM_SERVICE_CONTROL_START
	controlrotate				= 128,		//  - NSSM_SERVICE_CONTROL_ROTATE
	hookdeadline				= 60000,	// How many milliseconds to wait for a hook - NSSM_HOOK_DEADLINE
	threaddeadline				= 80000,	// How many milliseconds to wait for outstanding hooks - NSSM_HOOK_THREAD_DEADLINE
	cleanupdeadline				= 1500		// How many milliseconds to wait for closing logging thread - NSSM_CLEANUP_LOGGERS_DEADLINE
};


