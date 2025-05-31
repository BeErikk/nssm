#!/bin/sh
# written for and tested in ubuntu WSL bash
# Jerker Bäck 2025-05-25

find . -type f \( -name '*.h' -o -name '*.inl' -o -name '*.cpp' \) -exec sed -i \
	-e 's|\<NSSM_RESET_THROTTLE_RESTART\>|wait::reset_throttle_restart|g' \
	-e 's|\<NSSM_KILL_CONSOLE_GRACE_PERIOD\>|wait::kill_console_grace_period|g' \
	-e 's|\<NSSM_KILL_WINDOW_GRACE_PERIOD\>|wait::kill_window_grace_period|g' \
	-e 's|\<NSSM_KILL_THREADS_GRACE_PERIOD\>|wait::kill_threads_grace_period|g' \
	-e 's|\<NSSM_ROTATE_DELAY\>|wait::rotatedelay|g' \
	-e 's|\<NSSM_WAITHINT_MARGIN\>|wait::waithintmargin|g' \
	-e 's|\<NSSM_STOP_METHOD_CONSOLE\>|stopmethod::console|g' \
	-e 's|\<NSSM_STOP_METHOD_WINDOW\>|stopmethod::window|g' \
	-e 's|\<NSSM_STOP_METHOD_THREADS\>|stopmethod::threads|g' \
	-e 's|\<NSSM_STOP_METHOD_TERMINATE\>|stopmethod::terminate|g' \
	-e 's|\<NSSM_STARTUP_AUTOMATIC\>|startup::automatic|g' \
	-e 's|\<NSSM_STARTUP_DELAYED\>|startup::delayed|g' \
	-e 's|\<NSSM_STARTUP_MANUAL\>|startup::manual|g' \
	-e 's|\<NSSM_STARTUP_DISABLED\>|startup::disabled|g' \
	-e 's|\<NSSM_EXIT_RESTART\>|exit::restart|g' \
	-e 's|\<NSSM_EXIT_IGNORE\>|exit::ignore|g' \
	-e 's|\<NSSM_EXIT_REALLY\>|exit::really|g' \
	-e 's|\<NSSM_EXIT_UNCLEAN\>|exit::unclean|g' \
	-e 's|\<NSSM_NUM_EXIT_ACTIONS\>|exit::numactions|g' \
	-e 's|\<NSSM_REALTIME_PRIORITY\>|priority::realtime|g' \
	-e 's|\<NSSM_HIGH_PRIORITY\>|priority::high|g' \
	-e 's|\<NSSM_ABOVE_NORMAL_PRIORITY\>|priority::abovenormal|g' \
	-e 's|\<NSSM_NORMAL_PRIORITY\>|priority::normal|g' \
	-e 's|\<NSSM_BELOW_NORMAL_PRIORITY\>|priority::belownormal|g' \
	-e 's|\<NSSM_IDLE_PRIORITY\>|priority::idle|g' \
	-e 's|\<NSSM_SERVICE_STATUS_DEADLINE\>|wait::statusdeadline|g' \
	-e 's|\<NSSM_SERVICE_CONTROL_START\>|wait::controlstart|g' \
	-e 's|\<NSSM_SERVICE_CONTROL_ROTATE\>|wait::controlrotate|g' \
	-e 's|\<NSSM_HOOK_DEADLINE\>|wait::hookdeadline|g' \
	-e 's|\<NSSM_HOOK_THREAD_DEADLINE\>|wait::threaddeadline|g' \
	-e 's|\<NSSM_CLEANUP_LOGGERS_DEADLINE\>|wait::cleanupdeadline|g' \
{} \;
