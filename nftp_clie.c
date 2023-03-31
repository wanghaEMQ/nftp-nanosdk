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

#define MQTTV5 "aaaaaaaaa"

#ifdef MQTTV5

#define FTOPIC_ALLINONE "file/file-123"

#define FTOPIC_HELLO  FTOPIC_ALLINONE
#define FTOPIC_ACK    FTOPIC_ALLINONE
#define FTOPIC_BLOCKS FTOPIC_ALLINONE
#define FTOPIC_GIVEME FTOPIC_ALLINONE

#else

#define FTOPIC_HELLO "file/hello/file-123"
#define FTOPIC_ACK "file/ack/file-123"
#define FTOPIC_BLOCKS "file/blocks/file-123"
#define FTOPIC_GIVEME "file/giveme/file-123"

#endif

#define FURL "mqtt-tcp://127.0.0.1:1883"
#define FSENDERCLIENTID "file-123-sender"
#define FRECVERCLIENTID "file-123-recver"

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

#ifdef MQTTV5
	if ((rv = nng_mqttv5_client_open(sock)) != 0) {
		fatal("nng_socket", rv);
	}
#else
	if ((rv = nng_mqtt_client_open(sock)) != 0) {
		fatal("nng_socket", rv);
	}
#endif

	if ((rv = nng_dialer_create(dialer, *sock, url)) != 0) {
		fatal("nng_dialer_create", rv);
	}

	// create a CONNECT message
	/* CONNECT */
	nng_msg *connmsg;
	nng_mqtt_msg_alloc(&connmsg, 0);
	nng_mqtt_msg_set_packet_type(connmsg, NNG_MQTT_CONNECT);
#ifdef MQTTV5
	nng_mqtt_msg_set_connect_proto_version(connmsg, 5);
#else
	nng_mqtt_msg_set_connect_proto_version(connmsg, 4);
#endif
	nng_mqtt_msg_set_connect_keep_alive(connmsg, 60);
	nng_mqtt_msg_set_connect_client_id(connmsg, FSENDERCLIENTID);
	nng_mqtt_msg_set_connect_user_name(connmsg, "nng_mqtt_client");
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

static int g_wait = 1;

void
wait_ack_and_giveme(void *args)
{
	int rv;
	nng_socket sock = *(nng_socket *)args;
	while (true) {
		nng_msg *msg;
		uint8_t *payload;
		uint32_t payload_len;
		if ((rv = nng_recvmsg(sock, &msg, 0)) != 0) {
			fatal("nng_recvmsg", rv);
			continue;
		}

		// we should only receive publish messages
		if (nng_mqtt_msg_get_packet_type(msg) != NNG_MQTT_PUBLISH) {
			printf("NOT PUBLISH??? %d \n", nng_mqtt_msg_get_packet_type(msg));
			nng_msg_free(msg);
			continue;
		}

		payload = nng_mqtt_msg_get_publish_payload(msg, &payload_len);
		printf("Received payload length %d \n", payload_len);

		if (payload[0] == NFTP_TYPE_ACK) {
			g_wait = 0;
			nng_msg_free(msg);
			continue;
		}

		if (payload[0] != NFTP_TYPE_GIVEME) {
			printf("NOT GIVEME??? %d \n", payload[0]);
			nng_msg_free(msg);
			continue;
		}

		// handle giveme
		char * nftp_file_msg;
		int    nftp_file_len;
		if ((rv = nftp_proto_handler((char *)payload, payload_len, &nftp_file_msg, &nftp_file_len)) != 0) {
			printf("Error in handle giveme %d\n", rv);
			nng_msg_free(msg);
			continue;
		}
		client_publish(sock, FTOPIC_BLOCKS, (uint8_t *)nftp_file_msg, nftp_file_len, 1, 1);

		nng_msg_free(msg);
		free(nftp_file_msg);
	}
}

static void
send_callback(void *arg) {
	nng_mqtt_client *client = (nng_mqtt_client *) arg;
	nng_aio *aio = client->send_aio;
	nng_msg *msg = nng_aio_get_msg(aio);
	uint32_t count;
	uint8_t *code;
	code = (uint8_t *)nng_mqtt_msg_get_suback_return_codes(msg, &count);
	printf("sub done. aio mqtt result %d \n", nng_aio_result(aio));
	for (int i=0; i<count; ++i)
		printf("suback %x \n", code[i]);
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

#ifdef MQTTV5
	int count = 1;
#else
	int count = 2;
#endif

	nng_mqtt_topic_qos subscriptions[] = {
#ifdef MQTTV5
		{
		    .qos   = 0,
		    .topic = { 
				.buf    = (uint8_t *) FTOPIC_ALLINONE,
		        .length = strlen(FTOPIC_ALLINONE),
			},
			.nolocal = 1,
		},
#else
		{
		    .qos   = 0,
		    .topic = { 
				.buf    = (uint8_t *) FTOPIC_ACK,
		        .length = strlen(FTOPIC_ACK),
			},
		},
		{
		    .qos   = 0,
		    .topic = { 
				.buf    = (uint8_t *) FTOPIC_GIVEME,
		        .length = strlen(FTOPIC_GIVEME),
			},
		},
#endif
	};
	nng_mqtt_client *client = nng_mqtt_client_alloc(sock, &send_callback, true);
	nng_mqtt_subscribe_async(client, subscriptions, count, NULL);
	// Sync subscription
	// rv = nng_mqtt_subscribe(&sock, subscriptions, 1, NULL);

	// Asynchronous subscription
	nng_thread *thr;
	nng_thread_create(&thr, wait_ack_and_giveme, (void *)&sock);
	nng_msleep(1000);

	char fpath[256];
	while (true) {
		printf("/path/to/file==>>");
		if (gets(fpath) == NULL) {
			printf("Invalid input fpath\n");
			continue;
		}
		if (0 == nftp_file_exist(fpath)) {
			printf("%s is not exist\n", fpath);
			continue;
		}

		// Send a Hello
		char *nftp_hello_msg = NULL;
		int   nftp_hello_len = 0;
		rv = nftp_proto_maker(fpath, NFTP_TYPE_HELLO, 0, 0, &nftp_hello_msg, &nftp_hello_len);
		if (rv != 0)
			printf("hello make rv %d\n", rv);
		client_publish(sock, FTOPIC_HELLO, (uint8_t *)nftp_hello_msg, nftp_hello_len, 1, 1);

		// Wait an ACK
		printf("wait ack\n");
		// TODO condition variable
		while (g_wait == 1) {
			nng_msleep(500);
		}
		printf("get ack and start\n");

		size_t blocks = 0;
		rv = nftp_file_blocks(fpath, &blocks);
		if (rv != 0)
			printf("blocks rv %d\n", rv);
		printf("blocks %zu\n", blocks);
		nng_msleep(1000);

		// Send FILEs and END
		for (int i=0; i<blocks-1; ++i) {
			char *nftp_file_msg;
			int   nftp_file_len;
			if (i % 10 == 0) {
				printf("Cancel sending block %d to simulate poor network.\n", i);
				continue;
			}
			nftp_proto_maker(fpath, NFTP_TYPE_FILE, 0, i, &nftp_file_msg, &nftp_file_len);
			client_publish(sock, FTOPIC_BLOCKS, (uint8_t *)nftp_file_msg, nftp_file_len, 1, 1);
			// Assume 1 Mbps bandwidth
			// 1Mbps / 8bit * 32KB ~= 4 Packets/sec
			nng_msleep(250);
		}
		char *nftp_end_msg;
		int   nftp_end_len;
		nftp_proto_maker(fpath, NFTP_TYPE_END, 0, blocks-1, &nftp_end_msg, &nftp_end_len);
		client_publish(sock, FTOPIC_BLOCKS, (uint8_t *)nftp_end_msg, nftp_end_len, 1, 1);
		printf("done\n");
	}

	for (;;)
		nng_msleep(1000);
	// nng_mqtt_disconnect(&sock, 5, NULL);
	nftp_proto_fini();

	return 0;
}
