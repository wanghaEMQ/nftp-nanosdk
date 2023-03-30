// Author: wangha <wanghaemq at emq dot com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

//
// This is just a simple MQTT client demonstration application.
//
// The application has two sub-commands: `pub` and `sub`. The `pub`
// sub-command publishes a given message to the server and then exits.
// The `sub` sub-command subscribes to the given topic filter and blocks
// waiting for incoming messages.
//
// # Example:
//
// Publish 'hello' to `topic` with QoS `0`:
// ```
// $ ./mqtt_client pub mqtt-tcp://127.0.0.1:1883 0 topic hello
// ```
//
// Subscribe to `topic` with QoS `0` and waiting for messages:
// ```
// $ ./mqtt_client sub mqtt-tcp://127.0.0.1:1883 0 topic
// ```
//

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>

#include <nng/mqtt/mqtt_client.h>
#include <nng/nng.h>
#include <nng/supplemental/util/platform.h>

#include "nftp.h"

// Subcommands
#define PUBLISH "pub"
#define SUBSCRIBE "sub"

#define FTOPIC_HELLO "file/hello/file-123"
#define FTOPIC_ACK "file/ack/file-123"
#define FTOPIC_BLOCKS "file/blocks/file-123"
#define FTOPIC_GIVEME "file/giveme/file-123"
#define FURL "mqtt-tcp://127.0.0.1:1883"
#define FSENDERCLIENTID "file-sender"
#define FRECVERCLIENTID "file-recver"

static char *fname_curr = NULL;
static int   flen_curr = 0;

#define STN 5
static int stcnt = 0;
static int stats[STN] = {0};

static int stats_push(int v) {
	for (int i=0; i<stcnt; i++) {
		if (stats[i] != v) {
			stcnt = 1;
			stats[0] = v;
		}
	}
	// All value in stack are equal to V
	//
	// If stack is full
	if (stcnt == STN) {
		stcnt = 0;
		return v;
	}
	// If stack is not full
	stats[stcnt] = v;
	stcnt ++;
	return -1;
}

void
fatal(const char *msg, int rv)
{
	fprintf(stderr, "%s: %s\n", msg, nng_strerror(rv));
}

int keepRunning = 1;
void
intHandler(int dummy)
{
	keepRunning = 0;
	fprintf(stderr, "\nclient exit(0).\n");
	// nng_closeall();
	exit(0);
}

static void
disconnect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	int reason = 0;
	// get connect reason
	nng_pipe_get_int(p, NNG_OPT_MQTT_DISCONNECT_REASON, &reason);
	// property *prop;
	// nng_pipe_get_ptr(p, NNG_OPT_MQTT_DISCONNECT_PROPERTY, &prop);
	// nng_socket_get?
	printf("%s: disconnected! %d \n", __FUNCTION__, reason);
}

static void
connect_cb(nng_pipe p, nng_pipe_ev ev, void *arg)
{
	int reason;
	// get connect reason
	nng_pipe_get_int(p, NNG_OPT_MQTT_CONNECT_REASON, &reason);
	// get property for MQTT V5
	// property *prop;
	// nng_pipe_get_ptr(p, NNG_OPT_MQTT_CONNECT_PROPERTY, &prop);
	printf("%s: connected!\n", __FUNCTION__);
}

// Connect to the given address.
int
client_connect(
    nng_socket *sock, nng_dialer *dialer, const char *url, bool verbose)
{
	int        rv;

	if ((rv = nng_mqtt_client_open(sock)) != 0) {
		fatal("nng_socket", rv);
	}

	if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
		fatal("nng_dialer_create", rv);
	}

	// create a CONNECT message
	/* CONNECT */
	nng_msg *connmsg;
	nng_mqtt_msg_alloc(&connmsg, 0);
	nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
	nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
	nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
	nng_mqtt_msg_set_connect_client_id(connmsg, FRECVERCLIENTID);
	nng_mqtt_msg_set_connect_user_name(connmsg, "aaa");
	nng_mqtt_msg_set_connect_password(connmsg, "secrets");
	nng_mqtt_msg_set_connect_will_msg(
	    connmsg, (uint8_t *) "bye-bye", strlen("bye-bye"));
	nng_mqtt_msg_set_connect_will_topic(connmsg, "will_topic");
	nng_mqtt_msg_set_connect_clean_session(connmsg, true);

	nng_mqtt_set_connect_cb(*sock, connect_cb, sock);
	nng_mqtt_set_disconnect_cb(*sock, disconnect_cb, connmsg);

	uint8_t buff[1024] = { 0 };

	if (verbose) {
		nng_mqtt_msg_dump(connmsg, buff, sizeof(buff), true);
		printf("%s\n", buff);
	}

	printf("Connecting to server ...\n");
	nng_dialer_set_ptr(*dialer, NNG_OPT_MQTT_CONNMSG, connmsg);
	nng_dialer_start(*dialer, NNG_FLAG_NONBLOCK);

	return (0);
}

// Publish a message to the given topic and with the given QoS.
int
client_publish(nng_socket sock, const char *topic, uint8_t *payload,
    uint32_t payload_len, uint8_t qos, bool verbose)
{
	int rv;

	// create a PUBLISH message
	nng_msg *pubmsg;
	nng_mqtt_msg_alloc(&pubmsg, 0);
	nng_mqtt_msg_set_packet_type(pubmsg, NNG_MQTT_PUBLISH);
	nng_mqtt_msg_set_publish_dup(pubmsg, 0);
	nng_mqtt_msg_set_publish_qos(pubmsg, qos);
	nng_mqtt_msg_set_publish_retain(pubmsg, 0);
	nng_mqtt_msg_set_publish_payload(
	    pubmsg, (uint8_t *) payload, payload_len);
	nng_mqtt_msg_set_publish_topic(pubmsg, topic);

	// printf("Publishing to '%s' ...\n", topic);
	if ((rv = nng_sendmsg(sock, pubmsg, NNG_FLAG_NONBLOCK)) != 0) {
		fatal("nng_sendmsg", rv);
	}

	return rv;
}

static g_wait = 1;

void
ask_nextid(void *args)
{
	int rv;
	nng_socket sock = *(nng_socket *)args;
	while (true) {
		nng_msg *msg;
		uint8_t *payload;
		uint32_t payload_len;
		int blocks, nextid;

		nng_msleep(100);

		if (fname_curr == NULL) {
			continue;
		}

		if ((rv = nftp_proto_recv_status(fname_curr, &blocks, &nextid)) != 0) {
			printf("Done!!! The ctx of this file has been erase %s %d\n", fname_curr, rv);
			fname_curr = NULL;
			continue;
		}

		if (nextid > blocks-1) {
			// no more giveme needed
			printf("should not be here %d %d\n", nextid, blocks);
			continue;
		}

		if (stats_push(nextid) < 0) {
			continue;
		}
		printf("ask nextid %d\n", nextid);

		rv = nftp_proto_maker(fname_curr, NFTP_TYPE_GIVEME, 0, nextid, &payload, &payload_len);
		if (rv != 0) {
			printf("errror in make giveme %d\n", fname_curr, rv);
			continue;
		}

		client_publish(sock, FTOPIC_GIVEME, payload, payload_len, 1, 1);
	}
}

static void
send_callback(void *arg) {
	nng_mqtt_client *client = (nng_mqtt_client *) arg;
	nng_aio *aio = client->send_aio;
	nng_msg *msg = nng_aio_get_msg(aio);
	uint32_t count;
	reason_code *code;
	code = (reason_code *)nng_mqtt_msg_get_suback_return_codes(msg, &count);
	printf("aio mqtt result %d \n", nng_aio_result(aio));
	printf("suback %d \n", *code);
	nng_msg_free(msg);
}

int
main(const int argc, const char **argv)
{
	nng_socket sock;
	nng_dialer dailer;

	const char *exe = argv[0];

	const char *cmd = argv[1];

	const char *url         = FURL;
	int         rv          = 0;
	char *      verbose_env = getenv("VERBOSE");
	bool        verbose     = verbose_env && strlen(verbose_env) > 0;

	nftp_proto_init();

	client_connect(&sock, &dailer, url, verbose);

	signal(SIGINT, intHandler);

	int count = 2;
	nng_mqtt_topic_qos subscriptions[] = {
		{
		    .qos   = 0,
		    .topic = { 
				.buf    = (uint8_t *) FTOPIC_HELLO,
		        .length = strlen(FTOPIC_HELLO),
			},
		},
		{
		    .qos   = 0,
		    .topic = { 
				.buf    = (uint8_t *) FTOPIC_BLOCKS,
		        .length = strlen(FTOPIC_BLOCKS),
			},
		},

	};

	nng_msleep(1000);
	// Sync subscription
	// rv = nng_mqtt_subscribe(&sock, subscriptions, 1, NULL);

	// Asynchronous subscription
	nng_mqtt_client *client = nng_mqtt_client_alloc(sock, &send_callback, true);
	nng_mqtt_subscribe_async(client, subscriptions, count, NULL);
	printf("sub done\n");

	nng_thread *thr;
	nng_thread_create(&thr, ask_nextid, (void *)&sock);
	nng_msleep(1000);

	while(true) {
		nng_msg *msg;
		char    *nftp_reply_msg = NULL;
		int      nftp_reply_len = 0;
		uint8_t *payload;
		uint32_t payload_len;

		if ((rv = nng_recvmsg(sock, &msg, 0)) != 0) {
			fatal("nng_recvmsg", rv);
			continue;
		}

		// we should only receive publish messages
		if (nng_mqtt_msg_get_packet_type(msg) != NNG_MQTT_PUBLISH) {
			printf("NOT PUBLISH???\n");
			nng_msg_free(msg);
			continue;
		}

		payload = nng_mqtt_msg_get_publish_payload(msg, &payload_len);
		printf("Received payload %d \n", payload_len);

		rv = nftp_proto_handler(payload, payload_len, &nftp_reply_msg, &nftp_reply_len);
		if (rv != 0) {
			printf("Error in handling payload [%x] \n", payload[0]);
		}

		if (payload[0] == NFTP_TYPE_HELLO) {
			char *fname_;
			int   flen_;
			printf("Received HELLO");
			nftp_proto_hello_get_fname(payload, payload_len, &fname_, &flen_);

			fname_curr = strndup(fname_, flen_);
			// Ask_nextid start work until now. Ugly but works.

			printf("file name %s ..\n", fname_curr);
			printf("reply ack\n");
			client_publish(sock, FTOPIC_ACK, nftp_reply_msg, nftp_reply_len, 1, 1);
			free(nftp_reply_msg);

			nng_msg_free(msg);
			msg = NULL;
			continue;
		}

		if (payload[0] == NFTP_TYPE_FILE || payload[0] == NFTP_TYPE_END) {
			printf("Received FILE");
			free(nftp_reply_msg);

			nng_msg_free(msg);
			msg = NULL;
			continue;
		}

		printf("INVALID NFTP TYPE [%d]\n", payload[0]);
		nng_msg_free(msg);
		msg = NULL;
	}

	for (;;)
		nng_msleep(1000);
	// nng_mqtt_disconnect(&sock, 5, NULL);
	nftp_proto_fini();

	return 0;
}
