/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/un.h>
#include <poll.h>

#include <hardware/bluetooth.h>
#include <hardware/bt_av.h>
#include <hardware/bt_hh.h>
#include <hardware/bt_pan.h>
#include <hardware/bt_sock.h>
#include <hardware/bt_hf.h>
#include <hardware/bt_hl.h>
#include <hardware/bt_rc.h>

#include "textconv.h"

/* Interfaces from hal that can be populated during application lifetime */
extern const bt_interface_t *if_bluetooth;

/*
 * Structure defines top level interfaces that can be used in test tool
 * this will contain values as: adapter, av, gatt, sock, pan...
 */
struct interface {
	const char *name; /* interface name */
	struct method *methods; /* methods available for this interface */
};

extern const struct interface bluetooth_if;

/* Interfaces that will show up in tool (first part of command line) */
extern const struct interface *interfaces[];

#define METHOD(name, func, comp, help) {name, func, comp, help}
#define STD_METHOD(m) {#m, m##_p, NULL, NULL}
#define STD_METHODC(m) {#m, m##_p, m##_c, NULL}
#define STD_METHODH(m, h) {#m, m##_p, NULL, h}
#define STD_METHODCH(m, h) {#m, m##_p, m##_c, h}
#define END_METHOD {"", NULL, NULL, NULL}

/*
 * Function to parse argument for function, argv[0] and argv[1] are already
 * parsed before this function is called and contain interface and method name
 * up to argc - 1 arguments are finished and should be used to decide which
 * function enumeration function to return
 */
typedef void (*parse_and_call)(int argc, const char **argv);

/*
 * This is prototype of function that will return string for given number.
 * Purpose is to enumerate string for auto completion.
 * Function of this type will always be called in loop.
 * First time function is called i = 0, then if function returns non-NULL
 * it will be called again till for some value of i it will return NULL
 */
typedef const char *(*enum_func)(void *user, int i);

/*
 * This is prototype of function that when given argc, argv will
 * fill penum_func with pointer to function that will enumerate
 * parameters for argc argument, puser will be passed to penum_func.
 */
typedef void (*tab_complete)(int argc, const char **argv,
					enum_func *penum_func, void **puser);

/*
 * For each method there is name and two functions to parse command line
 * and call proper hal function on.
 */
struct method {
	const char *name;
	parse_and_call func;
	tab_complete complete;
	const char *help;
};

int haltest_error(const char *format, ...);
int haltest_info(const char *format, ...);
int haltest_warn(const char *format, ...);

/*
 * Enumerator for discovered devices, to be used as tab completion enum_func
 */
const char *enum_devices(void *v, int i);
void add_remote_device(const bt_bdaddr_t *addr);

/* Helper macro for executing function on interface and printing BT_STATUS */
#define EXEC(f, ...) \
	{ \
		int err = f(__VA_ARGS__); \
		haltest_info("%s: %s\n", #f, bt_status_t2str(err)); \
	}

/* Helper macro for executing void function on interface */
#define EXECV(f, ...) \
	{ \
		(void) f(__VA_ARGS__); \
		haltest_info("%s: void\n", #f); \
	}

#define RETURN_IF_NULL(x) \
	do { if (!x) { haltest_error("%s is NULL\n", #x); return; } } while (0)

#define VERIFY_ADDR_ARG(n, adr) \
	do { \
		if (n < argc) \
			str2bt_bdaddr_t(argv[n], adr); \
		else { \
			haltest_error("No address specified\n");\
			return;\
		} \
	} while (0)