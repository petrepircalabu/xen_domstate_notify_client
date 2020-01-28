/*   
 * client.c
 *
 * Generic netlink client to handle Xen domain state change notifications.
 *
 * Copyright (c) 2019 Bitdefender S.R.L.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR    
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netlink/msg.h>
#include <netlink/attr.h>

#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

static int interrupted;

/* TODO: move definitions to a common header */

/* GENL Interface */

#define DOMSTATE_NOTIFY_GENL_FAMILY_NAME        "domstate_notify"
#define DOMSTATE_NOTIFY_GENL_VERSION            0x01
#define DOMSTATE_NOTIFY_MCGROUP_NAME            "domstate_notify"

/* Supported commands */
enum {
        DOMSTATE_NOTIFY_CMD_UNSPEC,
        DOMSTATE_NOTIFY_CMD_OPEN,
        DOMSTATE_NOTIFY_CMD_DESTROY,
        __DOMSTATE_NOTIFY_CMD_MAX,
};
#define DOMSTATE_NOTIFY_CMD_MAX (__DOMSTATE_NOTIFY_CMD_MAX - 1)

/* Configuration policy attributes */
enum {
	DOMSTATE_NOTIFY_ATTR_UNSPEC,
	DOMSTATE_NOTIFY_ATTR_DOMAIN_ID,
	DOMSTATE_NOTIFY_ATTR_STATE,
	DOMSTATE_NOTIFY_ATTR_EXTRA,
	__DOMSTATE_NOTIFY_ATTR_MAX,
};
#define DOMSTATE_NOTIFY_ATTR_MAX (__DOMSTATE_NOTIFY_ATTR_MAX - 1)

static void close_handler(int sig)
{
	interrupted = sig;
}

static int callback_message(struct nl_msg *nlmsg, void *arg)
{
	struct genlmsghdr *nlhdr = nlmsg_data(nlmsg_hdr(nlmsg));
	struct nlattr *attr[DOMSTATE_NOTIFY_ATTR_MAX + 1];
	uint32_t domain_id, state, extra;

	printf("cmd = %d", nlhdr->cmd);

	nla_parse(attr, DOMSTATE_NOTIFY_ATTR_MAX, genlmsg_attrdata(nlhdr, 0),
			genlmsg_attrlen(nlhdr, 0), NULL);

	if (attr[DOMSTATE_NOTIFY_ATTR_DOMAIN_ID]) {
		domain_id = nla_get_u32(attr[DOMSTATE_NOTIFY_ATTR_DOMAIN_ID]);
		printf(" domain_id = %d", domain_id);
	}

	if (attr[DOMSTATE_NOTIFY_ATTR_STATE]) {
		state = nla_get_u32(attr[DOMSTATE_NOTIFY_ATTR_STATE]);
		printf(" state = %d", state);
	}

	if (attr[DOMSTATE_NOTIFY_ATTR_EXTRA]) {
		extra = nla_get_u32(attr[DOMSTATE_NOTIFY_ATTR_EXTRA]);
		printf(" extra = %d", extra);
	}

	printf("\n");

	return NL_OK;
}

int main(int argc, char* argv[])
{
	struct nl_sock* nlsock;
	struct sigaction act;
	int ret = 0, family_id, grp_id;

	/* ensure that if we get a signal, we'll do cleanup, then exit */
	act.sa_handler = close_handler;
	act.sa_flags = 0;
	sigemptyset(&act.sa_mask);
	sigaction(SIGHUP,  &act, NULL);
	sigaction(SIGTERM, &act, NULL);  
	sigaction(SIGINT,  &act, NULL);
	sigaction(SIGALRM, &act, NULL);

	nlsock = nl_socket_alloc();
	if ( !nlsock ) {
		fprintf(stderr, "Unable to alloc nl socket errno = %d\n", errno);
		exit(EXIT_FAILURE);
	}

	/* disable seq checks on multicast sockets */
	nl_socket_disable_seq_check(nlsock);
	nl_socket_modify_cb(nlsock, NL_CB_VALID, NL_CB_CUSTOM, callback_message, NULL);

	ret = genl_connect(nlsock);
	if (ret < 0) { 
		fprintf(stderr, "Failed to connect to netlink socket.\n"); 
		goto e_exit;
	}

	/* resolve the generic nl family id*/
	ret = genl_ctrl_resolve(nlsock, DOMSTATE_NOTIFY_GENL_FAMILY_NAME);
	if (ret < 0) {
		fprintf(stderr, "Unable to resolve family name %s\n",
				DOMSTATE_NOTIFY_GENL_FAMILY_NAME);
		goto e_exit;
	}
	family_id = ret;

	ret = genl_ctrl_resolve_grp(nlsock, DOMSTATE_NOTIFY_GENL_FAMILY_NAME,
			DOMSTATE_NOTIFY_MCGROUP_NAME);
	if (ret < 0) {
		fprintf(stderr, "Unable to resolve group %s\n",
				DOMSTATE_NOTIFY_MCGROUP_NAME);
		goto e_exit;
	}
	grp_id = ret;

	ret = nl_socket_add_membership(nlsock, grp_id);
	if ( ret < 0 ) {
		fprintf(stderr, "Unable to join group %s\n",
				DOMSTATE_NOTIFY_MCGROUP_NAME);
		goto e_exit;
	}

	ret = genl_send_simple(nlsock, family_id, DOMSTATE_NOTIFY_CMD_OPEN,
			DOMSTATE_NOTIFY_GENL_VERSION, 0);
	if (ret < 0) {
		fprintf(stderr, "Unable to send connect command!\n");
		goto e_exit;
	}

	nl_socket_set_nonblocking(nlsock);

	for (;;) {
		if (interrupted)
			break;
		ret = nl_recvmsgs_default(nlsock);
		if (ret < 0 && ret != -EINTR) {
			interrupted = 1;
		}
	}

	genl_send_simple(nlsock, family_id, DOMSTATE_NOTIFY_CMD_DESTROY,
			DOMSTATE_NOTIFY_GENL_VERSION, 0);

e_exit:
	nl_close(nlsock);
	nl_socket_free(nlsock);
	return ret;
}
