// SPDX-License-Identifier: GPL-2.0
/* Multipath TCP
 *
 * Copyright (c) 2021, Red Hat.
 */

#define pr_fmt(fmt) "MPTCP: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <net/sock.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/mptcp.h>
#include "protocol.h"

#define MIN_INFO_OPTLEN_SIZE		16
#define MIN_FULL_INFO_OPTLEN_SIZE	40

static struct sock *__mptcp_tcp_fallback(struct mptcp_sock *msk)
{
	msk_owned_by_me(msk);

	if (likely(!__mptcp_check_fallback(msk)))
		return NULL;

	return msk->first;
}

static u32 sockopt_seq_reset(const struct sock *sk)
{
	sock_owned_by_me(sk);

	/* Highbits contain state.  Allows to distinguish sockopt_seq
	 * of listener and established:
	 * s0 = new_listener()
	 * sockopt(s0) - seq is 1
	 * s1 = accept(s0) - s1 inherits seq 1 if listener sk (s0)
	 * sockopt(s0) - seq increments to 2 on s0
	 * sockopt(s1) // seq increments to 2 on s1 (different option)
	 * new ssk completes join, inherits options from s0 // seq 2
	 * Needs sync from mptcp join logic, but ssk->seq == msk->seq
	 *
	 * Set High order bits to sk_state so ssk->seq == msk->seq test
	 * will fail.
	 */

	return (u32)sk->sk_state << 24u;
}

static void sockopt_seq_inc(struct mptcp_sock *msk)
{
	u32 seq = (msk->setsockopt_seq + 1) & 0x00ffffff;

	msk->setsockopt_seq = sockopt_seq_reset((struct sock *)msk) + seq;
}

static int mptcp_get_int_option(struct mptcp_sock *msk, sockptr_t optval,
				unsigned int optlen, int *val)
{
	if (optlen < sizeof(int))
		return -EINVAL;

	if (copy_from_sockptr(val, optval, sizeof(*val)))
		return -EFAULT;

	return 0;
}

static void mptcp_sol_socket_sync_intval(struct mptcp_sock *msk, int optname, int val)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;

	lock_sock(sk);
	sockopt_seq_inc(msk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast(ssk);

		switch (optname) {
		case SO_DEBUG:
			sock_valbool_flag(ssk, SOCK_DBG, !!val);
			break;
		case SO_KEEPALIVE:
			if (ssk->sk_prot->keepalive)
				ssk->sk_prot->keepalive(ssk, !!val);
			sock_valbool_flag(ssk, SOCK_KEEPOPEN, !!val);
			break;
		case SO_PRIORITY:
			WRITE_ONCE(ssk->sk_priority, val);
			break;
		case SO_SNDBUF:
		case SO_SNDBUFFORCE:
			ssk->sk_userlocks |= SOCK_SNDBUF_LOCK;
			WRITE_ONCE(ssk->sk_sndbuf, sk->sk_sndbuf);
			mptcp_subflow_ctx(ssk)->cached_sndbuf = sk->sk_sndbuf;
			break;
		case SO_RCVBUF:
		case SO_RCVBUFFORCE:
			ssk->sk_userlocks |= SOCK_RCVBUF_LOCK;
			WRITE_ONCE(ssk->sk_rcvbuf, sk->sk_rcvbuf);
			break;
		case SO_MARK:
			if (READ_ONCE(ssk->sk_mark) != sk->sk_mark) {
				WRITE_ONCE(ssk->sk_mark, sk->sk_mark);
				sk_dst_reset(ssk);
			}
			break;
		case SO_INCOMING_CPU:
			WRITE_ONCE(ssk->sk_incoming_cpu, val);
			break;
		}

		subflow->setsockopt_seq = msk->setsockopt_seq;
		unlock_sock_fast(ssk, slow);
	}

	release_sock(sk);
}

static int mptcp_sol_socket_intval(struct mptcp_sock *msk, int optname, int val)
{
	sockptr_t optval = KERNEL_SOCKPTR(&val);
	struct sock *sk = (struct sock *)msk;
	int ret;

	ret = sock_setsockopt(sk->sk_socket, SOL_SOCKET, optname,
			      optval, sizeof(val));
	if (ret)
		return ret;

	mptcp_sol_socket_sync_intval(msk, optname, val);
	return 0;
}

static void mptcp_so_incoming_cpu(struct mptcp_sock *msk, int val)
{
	struct sock *sk = (struct sock *)msk;

	WRITE_ONCE(sk->sk_incoming_cpu, val);

	mptcp_sol_socket_sync_intval(msk, SO_INCOMING_CPU, val);
}

static int mptcp_setsockopt_sol_socket_tstamp(struct mptcp_sock *msk, int optname, int val)
{
	sockptr_t optval = KERNEL_SOCKPTR(&val);
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	int ret;

	ret = sock_setsockopt(sk->sk_socket, SOL_SOCKET, optname,
			      optval, sizeof(val));
	if (ret)
		return ret;

	lock_sock(sk);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast(ssk);

		sock_set_timestamp(sk, optname, !!val);
		unlock_sock_fast(ssk, slow);
	}

	release_sock(sk);
	return 0;
}

static int mptcp_setsockopt_sol_socket_int(struct mptcp_sock *msk, int optname,
					   sockptr_t optval,
					   unsigned int optlen)
{
	int val, ret;

	ret = mptcp_get_int_option(msk, optval, optlen, &val);
	if (ret)
		return ret;

	switch (optname) {
	case SO_KEEPALIVE:
	case SO_DEBUG:
	case SO_MARK:
	case SO_PRIORITY:
	case SO_SNDBUF:
	case SO_SNDBUFFORCE:
	case SO_RCVBUF:
	case SO_RCVBUFFORCE:
		return mptcp_sol_socket_intval(msk, optname, val);
	case SO_INCOMING_CPU:
		mptcp_so_incoming_cpu(msk, val);
		return 0;
	case SO_TIMESTAMP_OLD:
	case SO_TIMESTAMP_NEW:
	case SO_TIMESTAMPNS_OLD:
	case SO_TIMESTAMPNS_NEW:
		return mptcp_setsockopt_sol_socket_tstamp(msk, optname, val);
	}

	return -ENOPROTOOPT;
}

static int mptcp_setsockopt_sol_socket_timestamping(struct mptcp_sock *msk,
						    int optname,
						    sockptr_t optval,
						    unsigned int optlen)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	struct so_timestamping timestamping;
	int ret;

	if (optlen == sizeof(timestamping)) {
		if (copy_from_sockptr(&timestamping, optval,
				      sizeof(timestamping)))
			return -EFAULT;
	} else if (optlen == sizeof(int)) {
		memset(&timestamping, 0, sizeof(timestamping));

		if (copy_from_sockptr(&timestamping.flags, optval, sizeof(int)))
			return -EFAULT;
	} else {
		return -EINVAL;
	}

	ret = sock_setsockopt(sk->sk_socket, SOL_SOCKET, optname,
			      KERNEL_SOCKPTR(&timestamping),
			      sizeof(timestamping));
	if (ret)
		return ret;

	lock_sock(sk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast(ssk);

		sock_set_timestamping(sk, optname, timestamping);
		unlock_sock_fast(ssk, slow);
	}

	release_sock(sk);

	return 0;
}

static int mptcp_setsockopt_sol_socket_linger(struct mptcp_sock *msk, sockptr_t optval,
					      unsigned int optlen)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	struct linger ling;
	sockptr_t kopt;
	int ret;

	if (optlen < sizeof(ling))
		return -EINVAL;

	if (copy_from_sockptr(&ling, optval, sizeof(ling)))
		return -EFAULT;

	kopt = KERNEL_SOCKPTR(&ling);
	ret = sock_setsockopt(sk->sk_socket, SOL_SOCKET, SO_LINGER, kopt, sizeof(ling));
	if (ret)
		return ret;

	lock_sock(sk);
	sockopt_seq_inc(msk);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow = lock_sock_fast(ssk);

		if (!ling.l_onoff) {
			sock_reset_flag(ssk, SOCK_LINGER);
		} else {
			ssk->sk_lingertime = sk->sk_lingertime;
			sock_set_flag(ssk, SOCK_LINGER);
		}

		subflow->setsockopt_seq = msk->setsockopt_seq;
		unlock_sock_fast(ssk, slow);
	}

	release_sock(sk);
	return 0;
}

static int mptcp_setsockopt_sol_socket(struct mptcp_sock *msk, int optname,
				       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	struct sock *ssk;
	int ret;

	switch (optname) {
	case SO_REUSEPORT:
	case SO_REUSEADDR:
	case SO_BINDTODEVICE:
	case SO_BINDTOIFINDEX:
		lock_sock(sk);
		ssk = __mptcp_nmpc_sk(msk);
		if (IS_ERR(ssk)) {
			release_sock(sk);
			return PTR_ERR(ssk);
		}

		ret = sk_setsockopt(ssk, SOL_SOCKET, optname, optval, optlen);
		if (ret == 0) {
			if (optname == SO_REUSEPORT)
				sk->sk_reuseport = ssk->sk_reuseport;
			else if (optname == SO_REUSEADDR)
				sk->sk_reuse = ssk->sk_reuse;
			else if (optname == SO_BINDTODEVICE)
				sk->sk_bound_dev_if = ssk->sk_bound_dev_if;
			else if (optname == SO_BINDTOIFINDEX)
				sk->sk_bound_dev_if = ssk->sk_bound_dev_if;
		}
		release_sock(sk);
		return ret;
	case SO_KEEPALIVE:
	case SO_PRIORITY:
	case SO_SNDBUF:
	case SO_SNDBUFFORCE:
	case SO_RCVBUF:
	case SO_RCVBUFFORCE:
	case SO_MARK:
	case SO_INCOMING_CPU:
	case SO_DEBUG:
	case SO_TIMESTAMP_OLD:
	case SO_TIMESTAMP_NEW:
	case SO_TIMESTAMPNS_OLD:
	case SO_TIMESTAMPNS_NEW:
		return mptcp_setsockopt_sol_socket_int(msk, optname, optval,
						       optlen);
	case SO_TIMESTAMPING_OLD:
	case SO_TIMESTAMPING_NEW:
		return mptcp_setsockopt_sol_socket_timestamping(msk, optname,
								optval, optlen);
	case SO_LINGER:
		return mptcp_setsockopt_sol_socket_linger(msk, optval, optlen);
	case SO_RCVLOWAT:
	case SO_RCVTIMEO_OLD:
	case SO_RCVTIMEO_NEW:
	case SO_SNDTIMEO_OLD:
	case SO_SNDTIMEO_NEW:
	case SO_BUSY_POLL:
	case SO_PREFER_BUSY_POLL:
	case SO_BUSY_POLL_BUDGET:
		/* No need to copy: only relevant for msk */
		return sock_setsockopt(sk->sk_socket, SOL_SOCKET, optname, optval, optlen);
	case SO_NO_CHECK:
	case SO_DONTROUTE:
	case SO_BROADCAST:
	case SO_BSDCOMPAT:
	case SO_PASSCRED:
	case SO_PASSPIDFD:
	case SO_PASSSEC:
	case SO_RXQ_OVFL:
	case SO_WIFI_STATUS:
	case SO_NOFCS:
	case SO_SELECT_ERR_QUEUE:
		return 0;
	}

	/* SO_OOBINLINE is not supported, let's avoid the related mess
	 * SO_ATTACH_FILTER, SO_ATTACH_BPF, SO_ATTACH_REUSEPORT_CBPF,
	 * SO_DETACH_REUSEPORT_BPF, SO_DETACH_FILTER, SO_LOCK_FILTER,
	 * we must be careful with subflows
	 *
	 * SO_ATTACH_REUSEPORT_EBPF is not supported, at it checks
	 * explicitly the sk_protocol field
	 *
	 * SO_PEEK_OFF is unsupported, as it is for plain TCP
	 * SO_MAX_PACING_RATE is unsupported, we must be careful with subflows
	 * SO_CNX_ADVICE is currently unsupported, could possibly be relevant,
	 * but likely needs careful design
	 *
	 * SO_ZEROCOPY is currently unsupported, TODO in sndmsg
	 * SO_TXTIME is currently unsupported
	 */

	return -EOPNOTSUPP;
}

static int mptcp_setsockopt_v6(struct mptcp_sock *msk, int optname,
			       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	int ret = -EOPNOTSUPP;
	struct sock *ssk;

	switch (optname) {
	case IPV6_V6ONLY:
	case IPV6_TRANSPARENT:
	case IPV6_FREEBIND:
		lock_sock(sk);
		ssk = __mptcp_nmpc_sk(msk);
		if (IS_ERR(ssk)) {
			release_sock(sk);
			return PTR_ERR(ssk);
		}

		ret = tcp_setsockopt(ssk, SOL_IPV6, optname, optval, optlen);
		if (ret != 0) {
			release_sock(sk);
			return ret;
		}

		sockopt_seq_inc(msk);

		switch (optname) {
		case IPV6_V6ONLY:
			sk->sk_ipv6only = ssk->sk_ipv6only;
			break;
		case IPV6_TRANSPARENT:
			inet_assign_bit(TRANSPARENT, sk,
					inet_test_bit(TRANSPARENT, ssk));
			break;
		case IPV6_FREEBIND:
			inet_assign_bit(FREEBIND, sk,
					inet_test_bit(FREEBIND, ssk));
			break;
		}

		release_sock(sk);
		break;
	}

	return ret;
}

static bool mptcp_supported_sockopt(int level, int optname)
{
	if (level == SOL_IP) {
		switch (optname) {
		/* should work fine */
		case IP_FREEBIND:
		case IP_TRANSPARENT:
		case IP_BIND_ADDRESS_NO_PORT:
		case IP_LOCAL_PORT_RANGE:

		/* the following are control cmsg related */
		case IP_PKTINFO:
		case IP_RECVTTL:
		case IP_RECVTOS:
		case IP_RECVOPTS:
		case IP_RETOPTS:
		case IP_PASSSEC:
		case IP_RECVORIGDSTADDR:
		case IP_CHECKSUM:
		case IP_RECVFRAGSIZE:

		/* common stuff that need some love */
		case IP_TOS:
		case IP_TTL:
		case IP_MTU_DISCOVER:
		case IP_RECVERR:

		/* possibly less common may deserve some love */
		case IP_MINTTL:

		/* the following is apparently a no-op for plain TCP */
		case IP_RECVERR_RFC4884:
			return true;
		}

		/* IP_OPTIONS is not supported, needs subflow care */
		/* IP_HDRINCL, IP_NODEFRAG are not supported, RAW specific */
		/* IP_MULTICAST_TTL, IP_MULTICAST_LOOP, IP_UNICAST_IF,
		 * IP_ADD_MEMBERSHIP, IP_ADD_SOURCE_MEMBERSHIP, IP_DROP_MEMBERSHIP,
		 * IP_DROP_SOURCE_MEMBERSHIP, IP_BLOCK_SOURCE, IP_UNBLOCK_SOURCE,
		 * MCAST_JOIN_GROUP, MCAST_LEAVE_GROUP MCAST_JOIN_SOURCE_GROUP,
		 * MCAST_LEAVE_SOURCE_GROUP, MCAST_BLOCK_SOURCE, MCAST_UNBLOCK_SOURCE,
		 * MCAST_MSFILTER, IP_MULTICAST_ALL are not supported, better not deal
		 * with mcast stuff
		 */
		/* IP_IPSEC_POLICY, IP_XFRM_POLICY are nut supported, unrelated here */
		return false;
	}
	if (level == SOL_IPV6) {
		switch (optname) {
		case IPV6_V6ONLY:

		/* the following are control cmsg related */
		case IPV6_RECVPKTINFO:
		case IPV6_2292PKTINFO:
		case IPV6_RECVHOPLIMIT:
		case IPV6_2292HOPLIMIT:
		case IPV6_RECVRTHDR:
		case IPV6_2292RTHDR:
		case IPV6_RECVHOPOPTS:
		case IPV6_2292HOPOPTS:
		case IPV6_RECVDSTOPTS:
		case IPV6_2292DSTOPTS:
		case IPV6_RECVTCLASS:
		case IPV6_FLOWINFO:
		case IPV6_RECVPATHMTU:
		case IPV6_RECVORIGDSTADDR:
		case IPV6_RECVFRAGSIZE:

		/* the following ones need some love but are quite common */
		case IPV6_TCLASS:
		case IPV6_TRANSPARENT:
		case IPV6_FREEBIND:
		case IPV6_PKTINFO:
		case IPV6_2292PKTOPTIONS:
		case IPV6_UNICAST_HOPS:
		case IPV6_MTU_DISCOVER:
		case IPV6_MTU:
		case IPV6_RECVERR:
		case IPV6_FLOWINFO_SEND:
		case IPV6_FLOWLABEL_MGR:
		case IPV6_MINHOPCOUNT:
		case IPV6_DONTFRAG:
		case IPV6_AUTOFLOWLABEL:

		/* the following one is a no-op for plain TCP */
		case IPV6_RECVERR_RFC4884:
			return true;
		}

		/* IPV6_HOPOPTS, IPV6_RTHDRDSTOPTS, IPV6_RTHDR, IPV6_DSTOPTS are
		 * not supported
		 */
		/* IPV6_MULTICAST_HOPS, IPV6_MULTICAST_LOOP, IPV6_UNICAST_IF,
		 * IPV6_MULTICAST_IF, IPV6_ADDRFORM,
		 * IPV6_ADD_MEMBERSHIP, IPV6_DROP_MEMBERSHIP, IPV6_JOIN_ANYCAST,
		 * IPV6_LEAVE_ANYCAST, IPV6_MULTICAST_ALL, MCAST_JOIN_GROUP, MCAST_LEAVE_GROUP,
		 * MCAST_JOIN_SOURCE_GROUP, MCAST_LEAVE_SOURCE_GROUP,
		 * MCAST_BLOCK_SOURCE, MCAST_UNBLOCK_SOURCE, MCAST_MSFILTER
		 * are not supported better not deal with mcast
		 */
		/* IPV6_ROUTER_ALERT, IPV6_ROUTER_ALERT_ISOLATE are not supported, since are evil */

		/* IPV6_IPSEC_POLICY, IPV6_XFRM_POLICY are not supported */
		/* IPV6_ADDR_PREFERENCES is not supported, we must be careful with subflows */
		return false;
	}
	if (level == SOL_TCP) {
		switch (optname) {
		/* the following are no-op or should work just fine */
		case TCP_THIN_DUPACK:
		case TCP_DEFER_ACCEPT:

		/* the following need some love */
		case TCP_MAXSEG:
		case TCP_NODELAY:
		case TCP_THIN_LINEAR_TIMEOUTS:
		case TCP_CONGESTION:
		case TCP_CORK:
		case TCP_KEEPIDLE:
		case TCP_KEEPINTVL:
		case TCP_KEEPCNT:
		case TCP_SYNCNT:
		case TCP_SAVE_SYN:
		case TCP_LINGER2:
		case TCP_WINDOW_CLAMP:
		case TCP_QUICKACK:
		case TCP_USER_TIMEOUT:
		case TCP_TIMESTAMP:
		case TCP_NOTSENT_LOWAT:
		case TCP_TX_DELAY:
		case TCP_INQ:
		case TCP_FASTOPEN:
		case TCP_FASTOPEN_CONNECT:
		case TCP_FASTOPEN_KEY:
		case TCP_FASTOPEN_NO_COOKIE:
			return true;
		}

		/* TCP_MD5SIG, TCP_MD5SIG_EXT are not supported, MD5 is not compatible with MPTCP */

		/* TCP_REPAIR, TCP_REPAIR_QUEUE, TCP_QUEUE_SEQ, TCP_REPAIR_OPTIONS,
		 * TCP_REPAIR_WINDOW are not supported, better avoid this mess
		 */
	}
	return false;
}

static int mptcp_setsockopt_sol_tcp_congestion(struct mptcp_sock *msk, sockptr_t optval,
					       unsigned int optlen)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	char name[TCP_CA_NAME_MAX];
	bool cap_net_admin;
	int ret;

	if (optlen < 1)
		return -EINVAL;

	ret = strncpy_from_sockptr(name, optval,
				   min_t(long, TCP_CA_NAME_MAX - 1, optlen));
	if (ret < 0)
		return -EFAULT;

	name[ret] = 0;

	cap_net_admin = ns_capable(sock_net(sk)->user_ns, CAP_NET_ADMIN);

	ret = 0;
	lock_sock(sk);
	sockopt_seq_inc(msk);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		int err;

		lock_sock(ssk);
		err = tcp_set_congestion_control(ssk, name, true, cap_net_admin);
		if (err < 0 && ret == 0)
			ret = err;
		subflow->setsockopt_seq = msk->setsockopt_seq;
		release_sock(ssk);
	}

	if (ret == 0)
		strscpy(msk->ca_name, name, sizeof(msk->ca_name));

	release_sock(sk);
	return ret;
}

static int __mptcp_setsockopt_set_val(struct mptcp_sock *msk, int max,
				      int (*set_val)(struct sock *, int),
				      int *msk_val, int val)
{
	struct mptcp_subflow_context *subflow;
	int err = 0;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		int ret;

		lock_sock(ssk);
		ret = set_val(ssk, val);
		err = err ? : ret;
		release_sock(ssk);
	}

	if (!err) {
		*msk_val = val;
		sockopt_seq_inc(msk);
	}

	return err;
}

static int __mptcp_setsockopt_sol_tcp_cork(struct mptcp_sock *msk, int val)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;

	sockopt_seq_inc(msk);
	msk->cork = !!val;
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		lock_sock(ssk);
		__tcp_sock_set_cork(ssk, !!val);
		release_sock(ssk);
	}
	if (!val)
		mptcp_check_and_set_pending(sk);

	return 0;
}

static int __mptcp_setsockopt_sol_tcp_nodelay(struct mptcp_sock *msk, int val)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;

	sockopt_seq_inc(msk);
	msk->nodelay = !!val;
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		lock_sock(ssk);
		__tcp_sock_set_nodelay(ssk, !!val);
		release_sock(ssk);
	}
	if (val)
		mptcp_check_and_set_pending(sk);
	return 0;
}

static int mptcp_setsockopt_sol_ip_set(struct mptcp_sock *msk, int optname,
				       sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	struct sock *ssk;
	int err;

	err = ip_setsockopt(sk, SOL_IP, optname, optval, optlen);
	if (err != 0)
		return err;

	lock_sock(sk);

	ssk = __mptcp_nmpc_sk(msk);
	if (IS_ERR(ssk)) {
		release_sock(sk);
		return PTR_ERR(ssk);
	}

	switch (optname) {
	case IP_FREEBIND:
		inet_assign_bit(FREEBIND, ssk, inet_test_bit(FREEBIND, sk));
		break;
	case IP_TRANSPARENT:
		inet_assign_bit(TRANSPARENT, ssk,
				inet_test_bit(TRANSPARENT, sk));
		break;
	case IP_BIND_ADDRESS_NO_PORT:
		inet_assign_bit(BIND_ADDRESS_NO_PORT, ssk,
				inet_test_bit(BIND_ADDRESS_NO_PORT, sk));
		break;
	case IP_LOCAL_PORT_RANGE:
		WRITE_ONCE(inet_sk(ssk)->local_port_range,
			   READ_ONCE(inet_sk(sk)->local_port_range));
		break;
	default:
		release_sock(sk);
		WARN_ON_ONCE(1);
		return -EOPNOTSUPP;
	}

	sockopt_seq_inc(msk);
	release_sock(sk);
	return 0;
}

static int mptcp_setsockopt_v4_set_tos(struct mptcp_sock *msk, int optname,
				       sockptr_t optval, unsigned int optlen)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	int err, val;

	err = ip_setsockopt(sk, SOL_IP, optname, optval, optlen);

	if (err != 0)
		return err;

	lock_sock(sk);
	sockopt_seq_inc(msk);
	val = READ_ONCE(inet_sk(sk)->tos);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow;

		slow = lock_sock_fast(ssk);
		__ip_sock_set_tos(ssk, val);
		unlock_sock_fast(ssk, slow);
	}
	release_sock(sk);

	return 0;
}

static int mptcp_setsockopt_v4(struct mptcp_sock *msk, int optname,
			       sockptr_t optval, unsigned int optlen)
{
	switch (optname) {
	case IP_FREEBIND:
	case IP_TRANSPARENT:
	case IP_BIND_ADDRESS_NO_PORT:
	case IP_LOCAL_PORT_RANGE:
		return mptcp_setsockopt_sol_ip_set(msk, optname, optval, optlen);
	case IP_TOS:
		return mptcp_setsockopt_v4_set_tos(msk, optname, optval, optlen);
	}

	return -EOPNOTSUPP;
}

static int mptcp_setsockopt_first_sf_only(struct mptcp_sock *msk, int level, int optname,
					  sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (struct sock *)msk;
	struct sock *ssk;
	int ret;

	/* Limit to first subflow, before the connection establishment */
	lock_sock(sk);
	ssk = __mptcp_nmpc_sk(msk);
	if (IS_ERR(ssk)) {
		ret = PTR_ERR(ssk);
		goto unlock;
	}

	ret = tcp_setsockopt(ssk, level, optname, optval, optlen);

unlock:
	release_sock(sk);
	return ret;
}

static int mptcp_setsockopt_all_sf(struct mptcp_sock *msk, int level,
				   int optname, sockptr_t optval,
				   unsigned int optlen)
{
	struct mptcp_subflow_context *subflow;
	int ret = 0;

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		ret = tcp_setsockopt(ssk, level, optname, optval, optlen);
		if (ret)
			break;
	}
	return ret;
}

static int mptcp_setsockopt_sol_tcp(struct mptcp_sock *msk, int optname,
				    sockptr_t optval, unsigned int optlen)
{
	struct sock *sk = (void *)msk;
	int ret, val;

	switch (optname) {
	case TCP_ULP:
		return -EOPNOTSUPP;
	case TCP_CONGESTION:
		return mptcp_setsockopt_sol_tcp_congestion(msk, optval, optlen);
	case TCP_DEFER_ACCEPT:
		/* See tcp.c: TCP_DEFER_ACCEPT does not fail */
		mptcp_setsockopt_first_sf_only(msk, SOL_TCP, optname, optval, optlen);
		return 0;
	case TCP_FASTOPEN:
	case TCP_FASTOPEN_CONNECT:
	case TCP_FASTOPEN_KEY:
	case TCP_FASTOPEN_NO_COOKIE:
		return mptcp_setsockopt_first_sf_only(msk, SOL_TCP, optname,
						      optval, optlen);
	}

	ret = mptcp_get_int_option(msk, optval, optlen, &val);
	if (ret)
		return ret;

	lock_sock(sk);
	switch (optname) {
	case TCP_INQ:
		if (val < 0 || val > 1)
			ret = -EINVAL;
		else
			msk->recvmsg_inq = !!val;
		break;
	case TCP_NOTSENT_LOWAT:
		WRITE_ONCE(msk->notsent_lowat, val);
		mptcp_write_space(sk);
		break;
	case TCP_CORK:
		ret = __mptcp_setsockopt_sol_tcp_cork(msk, val);
		break;
	case TCP_NODELAY:
		ret = __mptcp_setsockopt_sol_tcp_nodelay(msk, val);
		break;
	case TCP_KEEPIDLE:
		ret = __mptcp_setsockopt_set_val(msk, MAX_TCP_KEEPIDLE,
						 &tcp_sock_set_keepidle_locked,
						 &msk->keepalive_idle, val);
		break;
	case TCP_KEEPINTVL:
		ret = __mptcp_setsockopt_set_val(msk, MAX_TCP_KEEPINTVL,
						 &tcp_sock_set_keepintvl,
						 &msk->keepalive_intvl, val);
		break;
	case TCP_KEEPCNT:
		ret = __mptcp_setsockopt_set_val(msk, MAX_TCP_KEEPCNT,
						 &tcp_sock_set_keepcnt,
						 &msk->keepalive_cnt,
						 val);
		break;
	case TCP_MAXSEG:
		msk->maxseg = val;
		ret = mptcp_setsockopt_all_sf(msk, SOL_TCP, optname, optval,
					      optlen);
		break;
	default:
		ret = -ENOPROTOOPT;
	}

	release_sock(sk);
	return ret;
}

int mptcp_setsockopt(struct sock *sk, int level, int optname,
		     sockptr_t optval, unsigned int optlen)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p\n", msk);

	if (level == SOL_SOCKET)
		return mptcp_setsockopt_sol_socket(msk, optname, optval, optlen);

	if (!mptcp_supported_sockopt(level, optname))
		return -ENOPROTOOPT;

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when TCP socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssk = __mptcp_tcp_fallback(msk);
	release_sock(sk);
	if (ssk)
		return tcp_setsockopt(ssk, level, optname, optval, optlen);

	if (level == SOL_IP)
		return mptcp_setsockopt_v4(msk, optname, optval, optlen);

	if (level == SOL_IPV6)
		return mptcp_setsockopt_v6(msk, optname, optval, optlen);

	if (level == SOL_TCP)
		return mptcp_setsockopt_sol_tcp(msk, optname, optval, optlen);

	return -EOPNOTSUPP;
}

static int mptcp_getsockopt_first_sf_only(struct mptcp_sock *msk, int level, int optname,
					  char __user *optval, int __user *optlen)
{
	struct sock *sk = (struct sock *)msk;
	struct sock *ssk;
	int ret;

	lock_sock(sk);
	ssk = msk->first;
	if (ssk)
		goto get;

	ssk = __mptcp_nmpc_sk(msk);
	if (IS_ERR(ssk)) {
		ret = PTR_ERR(ssk);
		goto out;
	}

get:
	ret = tcp_getsockopt(ssk, level, optname, optval, optlen);

out:
	release_sock(sk);
	return ret;
}

void mptcp_diag_fill_info(struct mptcp_sock *msk, struct mptcp_info *info)
{
	struct sock *sk = (struct sock *)msk;
	u32 flags = 0;
	bool slow;
	u32 now;

	memset(info, 0, sizeof(*info));

	info->mptcpi_subflows = READ_ONCE(msk->pm.subflows);
	info->mptcpi_add_addr_signal = READ_ONCE(msk->pm.add_addr_signaled);
	info->mptcpi_add_addr_accepted = READ_ONCE(msk->pm.add_addr_accepted);
	info->mptcpi_local_addr_used = READ_ONCE(msk->pm.local_addr_used);

	if (inet_sk_state_load(sk) == TCP_LISTEN)
		return;

	/* The following limits only make sense for the in-kernel PM */
	if (mptcp_pm_is_kernel(msk)) {
		info->mptcpi_subflows_max =
			mptcp_pm_get_subflows_max(msk);
		info->mptcpi_add_addr_signal_max =
			mptcp_pm_get_add_addr_signal_max(msk);
		info->mptcpi_add_addr_accepted_max =
			mptcp_pm_get_add_addr_accept_max(msk);
		info->mptcpi_local_addr_max =
			mptcp_pm_get_local_addr_max(msk);
	}

	if (__mptcp_check_fallback(msk))
		flags |= MPTCP_INFO_FLAG_FALLBACK;
	if (READ_ONCE(msk->can_ack))
		flags |= MPTCP_INFO_FLAG_REMOTE_KEY_RECEIVED;
	info->mptcpi_flags = flags;

	slow = lock_sock_fast(sk);
	info->mptcpi_csum_enabled = READ_ONCE(msk->csum_enabled);
	info->mptcpi_token = msk->token;
	info->mptcpi_write_seq = msk->write_seq;
	info->mptcpi_retransmits = inet_csk(sk)->icsk_retransmits;
	info->mptcpi_bytes_sent = msk->bytes_sent;
	info->mptcpi_bytes_received = msk->bytes_received;
	info->mptcpi_bytes_retrans = msk->bytes_retrans;
	info->mptcpi_subflows_total = info->mptcpi_subflows +
		__mptcp_has_initial_subflow(msk);
	now = tcp_jiffies32;
	info->mptcpi_last_data_sent = jiffies_to_msecs(now - msk->last_data_sent);
	info->mptcpi_last_data_recv = jiffies_to_msecs(now - msk->last_data_recv);
	unlock_sock_fast(sk, slow);

	mptcp_data_lock(sk);
	info->mptcpi_last_ack_recv = jiffies_to_msecs(now - msk->last_ack_recv);
	info->mptcpi_snd_una = msk->snd_una;
	info->mptcpi_rcv_nxt = msk->ack_seq;
	info->mptcpi_bytes_acked = msk->bytes_acked;
	mptcp_data_unlock(sk);
}
EXPORT_SYMBOL_GPL(mptcp_diag_fill_info);

static int mptcp_getsockopt_info(struct mptcp_sock *msk, char __user *optval, int __user *optlen)
{
	struct mptcp_info m_info;
	int len;

	if (get_user(len, optlen))
		return -EFAULT;

	/* When used only to check if a fallback to TCP happened. */
	if (len == 0)
		return 0;

	len = min_t(unsigned int, len, sizeof(struct mptcp_info));

	mptcp_diag_fill_info(msk, &m_info);

	if (put_user(len, optlen))
		return -EFAULT;

	if (copy_to_user(optval, &m_info, len))
		return -EFAULT;

	return 0;
}

static int mptcp_put_subflow_data(struct mptcp_subflow_data *sfd,
				  char __user *optval,
				  u32 copied,
				  int __user *optlen)
{
	u32 copylen = min_t(u32, sfd->size_subflow_data, sizeof(*sfd));

	if (copied)
		copied += sfd->size_subflow_data;
	else
		copied = copylen;

	if (put_user(copied, optlen))
		return -EFAULT;

	if (copy_to_user(optval, sfd, copylen))
		return -EFAULT;

	return 0;
}

static int mptcp_get_subflow_data(struct mptcp_subflow_data *sfd,
				  char __user *optval,
				  int __user *optlen)
{
	int len, copylen;

	if (get_user(len, optlen))
		return -EFAULT;

	/* if mptcp_subflow_data size is changed, need to adjust
	 * this function to deal with programs using old version.
	 */
	BUILD_BUG_ON(sizeof(*sfd) != MIN_INFO_OPTLEN_SIZE);

	if (len < MIN_INFO_OPTLEN_SIZE)
		return -EINVAL;

	memset(sfd, 0, sizeof(*sfd));

	copylen = min_t(unsigned int, len, sizeof(*sfd));
	if (copy_from_user(sfd, optval, copylen))
		return -EFAULT;

	/* size_subflow_data is u32, but len is signed */
	if (sfd->size_subflow_data > INT_MAX ||
	    sfd->size_user > INT_MAX)
		return -EINVAL;

	if (sfd->size_subflow_data < MIN_INFO_OPTLEN_SIZE ||
	    sfd->size_subflow_data > len)
		return -EINVAL;

	if (sfd->num_subflows || sfd->size_kernel)
		return -EINVAL;

	return len - sfd->size_subflow_data;
}

static int mptcp_getsockopt_tcpinfo(struct mptcp_sock *msk, char __user *optval,
				    int __user *optlen)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	unsigned int sfcount = 0, copied = 0;
	struct mptcp_subflow_data sfd;
	char __user *infoptr;
	int len;

	len = mptcp_get_subflow_data(&sfd, optval, optlen);
	if (len < 0)
		return len;

	sfd.size_kernel = sizeof(struct tcp_info);
	sfd.size_user = min_t(unsigned int, sfd.size_user,
			      sizeof(struct tcp_info));

	infoptr = optval + sfd.size_subflow_data;

	lock_sock(sk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		++sfcount;

		if (len && len >= sfd.size_user) {
			struct tcp_info info;

			tcp_get_info(ssk, &info);

			if (copy_to_user(infoptr, &info, sfd.size_user)) {
				release_sock(sk);
				return -EFAULT;
			}

			infoptr += sfd.size_user;
			copied += sfd.size_user;
			len -= sfd.size_user;
		}
	}

	release_sock(sk);

	sfd.num_subflows = sfcount;

	if (mptcp_put_subflow_data(&sfd, optval, copied, optlen))
		return -EFAULT;

	return 0;
}

static void mptcp_get_sub_addrs(const struct sock *sk, struct mptcp_subflow_addrs *a)
{
	const struct inet_sock *inet = inet_sk(sk);

	memset(a, 0, sizeof(*a));

	if (sk->sk_family == AF_INET) {
		a->sin_local.sin_family = AF_INET;
		a->sin_local.sin_port = inet->inet_sport;
		a->sin_local.sin_addr.s_addr = inet->inet_rcv_saddr;

		if (!a->sin_local.sin_addr.s_addr)
			a->sin_local.sin_addr.s_addr = inet->inet_saddr;

		a->sin_remote.sin_family = AF_INET;
		a->sin_remote.sin_port = inet->inet_dport;
		a->sin_remote.sin_addr.s_addr = inet->inet_daddr;
#if IS_ENABLED(CONFIG_IPV6)
	} else if (sk->sk_family == AF_INET6) {
		const struct ipv6_pinfo *np = inet6_sk(sk);

		if (WARN_ON_ONCE(!np))
			return;

		a->sin6_local.sin6_family = AF_INET6;
		a->sin6_local.sin6_port = inet->inet_sport;

		if (ipv6_addr_any(&sk->sk_v6_rcv_saddr))
			a->sin6_local.sin6_addr = np->saddr;
		else
			a->sin6_local.sin6_addr = sk->sk_v6_rcv_saddr;

		a->sin6_remote.sin6_family = AF_INET6;
		a->sin6_remote.sin6_port = inet->inet_dport;
		a->sin6_remote.sin6_addr = sk->sk_v6_daddr;
#endif
	}
}

static int mptcp_getsockopt_subflow_addrs(struct mptcp_sock *msk, char __user *optval,
					  int __user *optlen)
{
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	unsigned int sfcount = 0, copied = 0;
	struct mptcp_subflow_data sfd;
	char __user *addrptr;
	int len;

	len = mptcp_get_subflow_data(&sfd, optval, optlen);
	if (len < 0)
		return len;

	sfd.size_kernel = sizeof(struct mptcp_subflow_addrs);
	sfd.size_user = min_t(unsigned int, sfd.size_user,
			      sizeof(struct mptcp_subflow_addrs));

	addrptr = optval + sfd.size_subflow_data;

	lock_sock(sk);

	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);

		++sfcount;

		if (len && len >= sfd.size_user) {
			struct mptcp_subflow_addrs a;

			mptcp_get_sub_addrs(ssk, &a);

			if (copy_to_user(addrptr, &a, sfd.size_user)) {
				release_sock(sk);
				return -EFAULT;
			}

			addrptr += sfd.size_user;
			copied += sfd.size_user;
			len -= sfd.size_user;
		}
	}

	release_sock(sk);

	sfd.num_subflows = sfcount;

	if (mptcp_put_subflow_data(&sfd, optval, copied, optlen))
		return -EFAULT;

	return 0;
}

static int mptcp_get_full_info(struct mptcp_full_info *mfi,
			       char __user *optval,
			       int __user *optlen)
{
	int len;

	BUILD_BUG_ON(offsetof(struct mptcp_full_info, mptcp_info) !=
		     MIN_FULL_INFO_OPTLEN_SIZE);

	if (get_user(len, optlen))
		return -EFAULT;

	if (len < MIN_FULL_INFO_OPTLEN_SIZE)
		return -EINVAL;

	memset(mfi, 0, sizeof(*mfi));
	if (copy_from_user(mfi, optval, MIN_FULL_INFO_OPTLEN_SIZE))
		return -EFAULT;

	if (mfi->size_tcpinfo_kernel ||
	    mfi->size_sfinfo_kernel ||
	    mfi->num_subflows)
		return -EINVAL;

	if (mfi->size_sfinfo_user > INT_MAX ||
	    mfi->size_tcpinfo_user > INT_MAX)
		return -EINVAL;

	return len - MIN_FULL_INFO_OPTLEN_SIZE;
}

static int mptcp_put_full_info(struct mptcp_full_info *mfi,
			       char __user *optval,
			       u32 copylen,
			       int __user *optlen)
{
	copylen += MIN_FULL_INFO_OPTLEN_SIZE;
	if (put_user(copylen, optlen))
		return -EFAULT;

	if (copy_to_user(optval, mfi, copylen))
		return -EFAULT;
	return 0;
}

static int mptcp_getsockopt_full_info(struct mptcp_sock *msk, char __user *optval,
				      int __user *optlen)
{
	unsigned int sfcount = 0, copylen = 0;
	struct mptcp_subflow_context *subflow;
	struct sock *sk = (struct sock *)msk;
	void __user *tcpinfoptr, *sfinfoptr;
	struct mptcp_full_info mfi;
	int len;

	len = mptcp_get_full_info(&mfi, optval, optlen);
	if (len < 0)
		return len;

	/* don't bother filling the mptcp info if there is not enough
	 * user-space-provided storage
	 */
	if (len > 0) {
		mptcp_diag_fill_info(msk, &mfi.mptcp_info);
		copylen += min_t(unsigned int, len, sizeof(struct mptcp_info));
	}

	mfi.size_tcpinfo_kernel = sizeof(struct tcp_info);
	mfi.size_tcpinfo_user = min_t(unsigned int, mfi.size_tcpinfo_user,
				      sizeof(struct tcp_info));
	sfinfoptr = u64_to_user_ptr(mfi.subflow_info);
	mfi.size_sfinfo_kernel = sizeof(struct mptcp_subflow_info);
	mfi.size_sfinfo_user = min_t(unsigned int, mfi.size_sfinfo_user,
				     sizeof(struct mptcp_subflow_info));
	tcpinfoptr = u64_to_user_ptr(mfi.tcp_info);

	lock_sock(sk);
	mptcp_for_each_subflow(msk, subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		struct mptcp_subflow_info sfinfo;
		struct tcp_info tcp_info;

		if (sfcount++ >= mfi.size_arrays_user)
			continue;

		/* fetch addr/tcp_info only if the user space buffers
		 * are wide enough
		 */
		memset(&sfinfo, 0, sizeof(sfinfo));
		sfinfo.id = subflow->subflow_id;
		if (mfi.size_sfinfo_user >
		    offsetof(struct mptcp_subflow_info, addrs))
			mptcp_get_sub_addrs(ssk, &sfinfo.addrs);
		if (copy_to_user(sfinfoptr, &sfinfo, mfi.size_sfinfo_user))
			goto fail_release;

		if (mfi.size_tcpinfo_user) {
			tcp_get_info(ssk, &tcp_info);
			if (copy_to_user(tcpinfoptr, &tcp_info,
					 mfi.size_tcpinfo_user))
				goto fail_release;
		}

		tcpinfoptr += mfi.size_tcpinfo_user;
		sfinfoptr += mfi.size_sfinfo_user;
	}
	release_sock(sk);

	mfi.num_subflows = sfcount;
	if (mptcp_put_full_info(&mfi, optval, copylen, optlen))
		return -EFAULT;

	return 0;

fail_release:
	release_sock(sk);
	return -EFAULT;
}

static int mptcp_put_int_option(struct mptcp_sock *msk, char __user *optval,
				int __user *optlen, int val)
{
	int len;

	if (get_user(len, optlen))
		return -EFAULT;
	if (len < 0)
		return -EINVAL;

	if (len < sizeof(int) && len > 0 && val >= 0 && val <= 255) {
		unsigned char ucval = (unsigned char)val;

		len = 1;
		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &ucval, 1))
			return -EFAULT;
	} else {
		len = min_t(unsigned int, len, sizeof(int));
		if (put_user(len, optlen))
			return -EFAULT;
		if (copy_to_user(optval, &val, len))
			return -EFAULT;
	}

	return 0;
}

static int mptcp_getsockopt_sol_tcp(struct mptcp_sock *msk, int optname,
				    char __user *optval, int __user *optlen)
{
	struct sock *sk = (void *)msk;

	switch (optname) {
	case TCP_ULP:
	case TCP_CONGESTION:
	case TCP_INFO:
	case TCP_CC_INFO:
	case TCP_DEFER_ACCEPT:
	case TCP_FASTOPEN:
	case TCP_FASTOPEN_CONNECT:
	case TCP_FASTOPEN_KEY:
	case TCP_FASTOPEN_NO_COOKIE:
		return mptcp_getsockopt_first_sf_only(msk, SOL_TCP, optname,
						      optval, optlen);
	case TCP_INQ:
		return mptcp_put_int_option(msk, optval, optlen, msk->recvmsg_inq);
	case TCP_CORK:
		return mptcp_put_int_option(msk, optval, optlen, msk->cork);
	case TCP_NODELAY:
		return mptcp_put_int_option(msk, optval, optlen, msk->nodelay);
	case TCP_KEEPIDLE:
		return mptcp_put_int_option(msk, optval, optlen,
					    msk->keepalive_idle ? :
					    READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_keepalive_time) / HZ);
	case TCP_KEEPINTVL:
		return mptcp_put_int_option(msk, optval, optlen,
					    msk->keepalive_intvl ? :
					    READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_keepalive_intvl) / HZ);
	case TCP_KEEPCNT:
		return mptcp_put_int_option(msk, optval, optlen,
					    msk->keepalive_cnt ? :
					    READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_keepalive_probes));
	case TCP_NOTSENT_LOWAT:
		return mptcp_put_int_option(msk, optval, optlen, msk->notsent_lowat);
	case TCP_IS_MPTCP:
		return mptcp_put_int_option(msk, optval, optlen, 1);
	case TCP_MAXSEG:
		return mptcp_getsockopt_first_sf_only(msk, SOL_TCP, optname,
						      optval, optlen);
	}
	return -EOPNOTSUPP;
}

static int mptcp_getsockopt_v4(struct mptcp_sock *msk, int optname,
			       char __user *optval, int __user *optlen)
{
	struct sock *sk = (void *)msk;

	switch (optname) {
	case IP_TOS:
		return mptcp_put_int_option(msk, optval, optlen, READ_ONCE(inet_sk(sk)->tos));
	case IP_FREEBIND:
		return mptcp_put_int_option(msk, optval, optlen,
				inet_test_bit(FREEBIND, sk));
	case IP_TRANSPARENT:
		return mptcp_put_int_option(msk, optval, optlen,
				inet_test_bit(TRANSPARENT, sk));
	case IP_BIND_ADDRESS_NO_PORT:
		return mptcp_put_int_option(msk, optval, optlen,
				inet_test_bit(BIND_ADDRESS_NO_PORT, sk));
	case IP_LOCAL_PORT_RANGE:
		return mptcp_put_int_option(msk, optval, optlen,
				READ_ONCE(inet_sk(sk)->local_port_range));
	}

	return -EOPNOTSUPP;
}

static int mptcp_getsockopt_v6(struct mptcp_sock *msk, int optname,
			       char __user *optval, int __user *optlen)
{
	struct sock *sk = (void *)msk;

	switch (optname) {
	case IPV6_V6ONLY:
		return mptcp_put_int_option(msk, optval, optlen,
					    sk->sk_ipv6only);
	case IPV6_TRANSPARENT:
		return mptcp_put_int_option(msk, optval, optlen,
					    inet_test_bit(TRANSPARENT, sk));
	case IPV6_FREEBIND:
		return mptcp_put_int_option(msk, optval, optlen,
					    inet_test_bit(FREEBIND, sk));
	}

	return -EOPNOTSUPP;
}

static int mptcp_getsockopt_sol_mptcp(struct mptcp_sock *msk, int optname,
				      char __user *optval, int __user *optlen)
{
	switch (optname) {
	case MPTCP_INFO:
		return mptcp_getsockopt_info(msk, optval, optlen);
	case MPTCP_FULL_INFO:
		return mptcp_getsockopt_full_info(msk, optval, optlen);
	case MPTCP_TCPINFO:
		return mptcp_getsockopt_tcpinfo(msk, optval, optlen);
	case MPTCP_SUBFLOW_ADDRS:
		return mptcp_getsockopt_subflow_addrs(msk, optval, optlen);
	}

	return -EOPNOTSUPP;
}

int mptcp_getsockopt(struct sock *sk, int level, int optname,
		     char __user *optval, int __user *option)
{
	struct mptcp_sock *msk = mptcp_sk(sk);
	struct sock *ssk;

	pr_debug("msk=%p\n", msk);

	/* @@ the meaning of setsockopt() when the socket is connected and
	 * there are multiple subflows is not yet defined. It is up to the
	 * MPTCP-level socket to configure the subflows until the subflow
	 * is in TCP fallback, when socket options are passed through
	 * to the one remaining subflow.
	 */
	lock_sock(sk);
	ssk = __mptcp_tcp_fallback(msk);
	release_sock(sk);
	if (ssk)
		return tcp_getsockopt(ssk, level, optname, optval, option);

	if (level == SOL_IP)
		return mptcp_getsockopt_v4(msk, optname, optval, option);
	if (level == SOL_IPV6)
		return mptcp_getsockopt_v6(msk, optname, optval, option);
	if (level == SOL_TCP)
		return mptcp_getsockopt_sol_tcp(msk, optname, optval, option);
	if (level == SOL_MPTCP)
		return mptcp_getsockopt_sol_mptcp(msk, optname, optval, option);
	return -EOPNOTSUPP;
}

static void sync_socket_options(struct mptcp_sock *msk, struct sock *ssk)
{
	static const unsigned int tx_rx_locks = SOCK_RCVBUF_LOCK | SOCK_SNDBUF_LOCK;
	struct sock *sk = (struct sock *)msk;

	if (ssk->sk_prot->keepalive) {
		if (sock_flag(sk, SOCK_KEEPOPEN))
			ssk->sk_prot->keepalive(ssk, 1);
		else
			ssk->sk_prot->keepalive(ssk, 0);
	}

	ssk->sk_priority = sk->sk_priority;
	ssk->sk_bound_dev_if = sk->sk_bound_dev_if;
	ssk->sk_incoming_cpu = sk->sk_incoming_cpu;
	ssk->sk_ipv6only = sk->sk_ipv6only;
	__ip_sock_set_tos(ssk, inet_sk(sk)->tos);

	if (sk->sk_userlocks & tx_rx_locks) {
		ssk->sk_userlocks |= sk->sk_userlocks & tx_rx_locks;
		if (sk->sk_userlocks & SOCK_SNDBUF_LOCK) {
			WRITE_ONCE(ssk->sk_sndbuf, sk->sk_sndbuf);
			mptcp_subflow_ctx(ssk)->cached_sndbuf = sk->sk_sndbuf;
		}
		if (sk->sk_userlocks & SOCK_RCVBUF_LOCK)
			WRITE_ONCE(ssk->sk_rcvbuf, sk->sk_rcvbuf);
	}

	if (sock_flag(sk, SOCK_LINGER)) {
		ssk->sk_lingertime = sk->sk_lingertime;
		sock_set_flag(ssk, SOCK_LINGER);
	} else {
		sock_reset_flag(ssk, SOCK_LINGER);
	}

	if (sk->sk_mark != ssk->sk_mark) {
		ssk->sk_mark = sk->sk_mark;
		sk_dst_reset(ssk);
	}

	sock_valbool_flag(ssk, SOCK_DBG, sock_flag(sk, SOCK_DBG));

	if (inet_csk(sk)->icsk_ca_ops != inet_csk(ssk)->icsk_ca_ops)
		tcp_set_congestion_control(ssk, msk->ca_name, false, true);
	__tcp_sock_set_cork(ssk, !!msk->cork);
	__tcp_sock_set_nodelay(ssk, !!msk->nodelay);
	tcp_sock_set_keepidle_locked(ssk, msk->keepalive_idle);
	tcp_sock_set_keepintvl(ssk, msk->keepalive_intvl);
	tcp_sock_set_keepcnt(ssk, msk->keepalive_cnt);
	tcp_sock_set_maxseg(ssk, msk->maxseg);

	inet_assign_bit(TRANSPARENT, ssk, inet_test_bit(TRANSPARENT, sk));
	inet_assign_bit(FREEBIND, ssk, inet_test_bit(FREEBIND, sk));
	inet_assign_bit(BIND_ADDRESS_NO_PORT, ssk, inet_test_bit(BIND_ADDRESS_NO_PORT, sk));
	WRITE_ONCE(inet_sk(ssk)->local_port_range, READ_ONCE(inet_sk(sk)->local_port_range));
}

void mptcp_sockopt_sync_locked(struct mptcp_sock *msk, struct sock *ssk)
{
	struct mptcp_subflow_context *subflow = mptcp_subflow_ctx(ssk);

	msk_owned_by_me(msk);

	ssk->sk_rcvlowat = 0;

	/* subflows must ignore any latency-related settings: will not affect
	 * the user-space - only the msk is relevant - but will foul the
	 * mptcp scheduler
	 */
	tcp_sk(ssk)->notsent_lowat = UINT_MAX;

	if (READ_ONCE(subflow->setsockopt_seq) != msk->setsockopt_seq) {
		sync_socket_options(msk, ssk);

		subflow->setsockopt_seq = msk->setsockopt_seq;
	}
}

/* unfortunately this is different enough from the tcp version so
 * that we can't factor it out
 */
int mptcp_set_rcvlowat(struct sock *sk, int val)
{
	struct mptcp_subflow_context *subflow;
	int space, cap;

	/* bpf can land here with a wrong sk type */
	if (sk->sk_protocol == IPPROTO_TCP)
		return -EINVAL;

	if (sk->sk_userlocks & SOCK_RCVBUF_LOCK)
		cap = sk->sk_rcvbuf >> 1;
	else
		cap = READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_rmem[2]) >> 1;
	val = min(val, cap);
	WRITE_ONCE(sk->sk_rcvlowat, val ? : 1);

	/* Check if we need to signal EPOLLIN right now */
	if (mptcp_epollin_ready(sk))
		sk->sk_data_ready(sk);

	if (sk->sk_userlocks & SOCK_RCVBUF_LOCK)
		return 0;

	space = mptcp_space_from_win(sk, val);
	if (space <= sk->sk_rcvbuf)
		return 0;

	/* propagate the rcvbuf changes to all the subflows */
	WRITE_ONCE(sk->sk_rcvbuf, space);
	mptcp_for_each_subflow(mptcp_sk(sk), subflow) {
		struct sock *ssk = mptcp_subflow_tcp_sock(subflow);
		bool slow;

		slow = lock_sock_fast(ssk);
		WRITE_ONCE(ssk->sk_rcvbuf, space);
		WRITE_ONCE(tcp_sk(ssk)->window_clamp, val);
		unlock_sock_fast(ssk, slow);
	}
	return 0;
}
