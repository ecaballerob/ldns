/*
 * net.c
 *
 * Network implementation
 * All network related functions are grouped here
 *
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004
 *
 * See the file LICENSE for the license
 */

#include <config.h>

#include <ldns/ldns.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>

#include "util.h"

ldns_pkt *
ldns_send(ldns_resolver *r, ldns_pkt *query_pkt)
{
	uint8_t i;
	
	struct sockaddr_storage *ns;
	struct sockaddr_in *ns4;
	struct sockaddr_in6 *ns6;
	socklen_t ns_len;
	struct timeval tv_s;
        struct timeval tv_e;

	ldns_rdf **ns_array;
	ldns_pkt *reply;
	ldns_buffer *qb;

	uint8_t *reply_bytes = NULL;
	size_t reply_size = 0;
	ldns_rdf *tsig_mac = NULL;

	if (!query_pkt) {
		/* nothing to do? */
		return NULL;
	}
	
	ns_array = ldns_resolver_nameservers(r);
	reply = NULL; ns_len = 0;
	
	qb = ldns_buffer_new(MAX_PACKETLEN);

	if (ldns_pkt_tsig(query_pkt)) {
		tsig_mac = ldns_rr_rdf(ldns_pkt_tsig(query_pkt), 3);
	}

	if (ldns_pkt2buffer_wire(qb, query_pkt) != LDNS_STATUS_OK) {
		return NULL;
	}


	/* loop through all defined nameservers */
	for (i = 0; i < ldns_resolver_nameserver_count(r); i++) {

		ns = ldns_rdf2native_sockaddr_storage(ns_array[i]);

		if ((ns->ss_family == AF_INET && 
				ldns_resolver_ip6(r) == LDNS_RESOLV_INET6)
				||
				(ns->ss_family == AF_INET6 &&
				 ldns_resolver_ip6(r) == LDNS_RESOLV_INET)) {
			/* mismatch, next please */
			continue;
		}

		/* setup some family specific stuff */
		switch(ns->ss_family) {
			case AF_INET:
				ns4 = (struct sockaddr_in*) ns;
				ns4->sin_port = htons(ldns_resolver_port(r));
				ns_len = (socklen_t)sizeof(struct sockaddr_in);
				break;
			case AF_INET6:
				ns6 = (struct sockaddr_in6*) ns;
				ns6->sin6_port = htons(ldns_resolver_port(r));
				ns_len = (socklen_t)sizeof(struct sockaddr_in6);
				break;
		}
		
		gettimeofday(&tv_s, NULL);
		/* query */
		if (1 == ldns_resolver_usevc(r)) {
			reply_bytes = ldns_send_tcp(qb, ns, ns_len, ldns_resolver_timeout(r), &reply_size);
		} else {
			/* udp here, please */
			reply_bytes = ldns_send_udp(qb, ns, ns_len, ldns_resolver_timeout(r), &reply_size);
		}
		
		/* obey the fail directive */
		if (!reply_bytes) {
			if (ldns_resolver_fail(r)) {
				return NULL;
			} else {
				continue;
			}
		} 
		
		if (ldns_wire2pkt(&reply, reply_bytes, reply_size) !=
		    LDNS_STATUS_OK) {
			FREE(reply_bytes);
			return NULL;
		}
		
		FREE(ns);
		gettimeofday(&tv_e, NULL);

		if (reply) {
			ldns_pkt_set_querytime(reply,
				((tv_e.tv_sec - tv_s.tv_sec) * 1000) +
				(tv_e.tv_usec - tv_s.tv_usec) / 1000);
			ldns_pkt_set_answerfrom(reply, ns_array[i]);
			ldns_pkt_set_when(reply, ctime((time_t*)&tv_s.tv_sec));
			break;
		} else {
			if (ldns_resolver_fail(r)) {
				/* if fail is set bail out, after the first
				 * one */
				break;
			}
		}

		/* wait retrans seconds... */
	}

	if (tsig_mac && reply_bytes) {
		if (!ldns_pkt_tsig_verify(reply,
		                          reply_bytes,
					  reply_size,
		                          ldns_resolver_tsig_keyname(r),
		                          ldns_resolver_tsig_keydata(r),
		                          tsig_mac)) {
			/* TODO: no print, feedback */
			dprintf("%s", ";; WARNING: TSIG VERIFICATION OF ANSWER FAILED!\n");
		}
	}
	
	FREE(reply_bytes);
	ldns_buffer_free(qb);
	return reply;
}

uint8_t *
ldns_send_udp(ldns_buffer *qbin, const struct sockaddr_storage *to, socklen_t tolen, struct timeval timeout, size_t *answer_size)
{
	int sockfd;
	ssize_t bytes;
	uint8_t *answer;
/*

	ldns_pkt *answer_pkt;
        struct timeval timeout;
        
        timeout.tv_sec = LDNS_DEFAULT_TIMEOUT_SEC;
        timeout.tv_usec = LDNS_DEFAULT_TIMEOUT_USEC;
*/        

	if ((sockfd = socket((int)((struct sockaddr*)to)->sa_family, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		dprintf("%s", "could not open socket\n");
		return NULL;
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
			(socklen_t) sizeof(timeout))) {
		perror("setsockopt");
		close(sockfd);
		return NULL;
	}
	
	bytes = sendto(sockfd, ldns_buffer_begin(qbin),
			ldns_buffer_position(qbin), 0, (struct sockaddr *)to, tolen);


	if (bytes == -1) {
		dprintf("%s", "error with sending\n");
		close(sockfd);
		return NULL;
	}

	if ((size_t) bytes != ldns_buffer_position(qbin)) {
		dprintf("%s", "amount mismatch\n");
		close(sockfd);
		return NULL;
	}
	
	/* wait for an response*/
	answer = XMALLOC(uint8_t, MAX_PACKETLEN);
	if (!answer) {
		dprintf("%s", "respons alloc error\n");
		return NULL;
	}

	bytes = recv(sockfd, answer, MAX_PACKETLEN, 0);

	close(sockfd);

	if (bytes == -1) {
		if (errno == EAGAIN) {
			dprintf("%s", "socket timeout\n");
		}
		FREE(answer);
		return NULL;
	}
	
	/* resize accordingly */
	answer = (uint8_t*)XREALLOC(answer, uint8_t *, (size_t) bytes);
	*answer_size = (size_t) bytes;

	return answer;
}

int
ldns_tcp_connect(const struct sockaddr_storage *to, socklen_t tolen, struct timeval timeout)
{
	int sockfd;
	
	if ((sockfd = socket((int)((struct sockaddr*)to)->sa_family, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("could not open socket");
		return 0;
	}

        if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                        (socklen_t) sizeof(timeout))) {
                perror("setsockopt");
                close(sockfd);
                return 0;
        }

	if (connect(sockfd, (struct sockaddr*)to, tolen) == -1) {
 		close(sockfd);
		perror("could not bind socket");
		return 0;
	}

	return sockfd;
}

ssize_t
ldns_tcp_send_query(ldns_buffer *qbin, int sockfd, const struct sockaddr_storage *to, socklen_t tolen)
{
	uint8_t *sendbuf;
	ssize_t bytes;

	/* add length of packet */
	sendbuf = XMALLOC(uint8_t, ldns_buffer_position(qbin) + 2);
	write_uint16(sendbuf, ldns_buffer_position(qbin));
	memcpy(sendbuf+2, ldns_buffer_export(qbin), ldns_buffer_position(qbin));

	bytes = sendto(sockfd, sendbuf,
			ldns_buffer_position(qbin)+2, 0, (struct sockaddr *)to, tolen);

        FREE(sendbuf);

	if (bytes == -1) {
		dprintf("%s", "error with sending\n");
		close(sockfd);
		return 0;
	}
	if ((size_t) bytes != ldns_buffer_position(qbin)+2) {
		dprintf("%s", "amount of sent bytes mismatch\n");
		close(sockfd);
		return 0;
	}
	
	return bytes;
}

/**
 * Creates a new ldns_pkt structure and reads the header data from the given
 * socket
 */
uint8_t *
ldns_tcp_read_wire(int sockfd, size_t *size)
{
	uint8_t *wire;
	uint16_t wire_size;
	ssize_t bytes = 0;

	wire = XMALLOC(uint8_t, 2);
	while (bytes < 2) {
		bytes = recv(sockfd, wire, 2, 0);
		if (bytes == -1) {
			if (errno == EAGAIN) {
				dprintf("%s", "socket timeout\n");
			}
			perror("error receiving tcp packet");
			return NULL;
		}
	}

	wire_size = read_uint16(wire);
	
	FREE(wire);
	wire = XMALLOC(uint8_t, wire_size);
	bytes = 0;

	while (bytes < (ssize_t) wire_size) {
		bytes += recv(sockfd, wire + bytes, (size_t) (wire_size - bytes), 0);
		if (bytes == -1) {
			if (errno == EAGAIN) {
				dprintf("%s", "socket timeout\n");
			}
			perror("error receiving tcp packet");
			FREE(wire);
			return NULL;
		}
	}
	
	*size = (size_t) bytes;

	return wire;
}

/* keep in mind that in DNS tcp messages the first 2 bytes signal the
 * amount data to expect
 */
uint8_t *
ldns_send_tcp(ldns_buffer *qbin, const struct sockaddr_storage *to, socklen_t tolen, struct timeval timeout, size_t *answer_size)
{
	int sockfd;
	uint8_t *answer;
	
	sockfd = ldns_tcp_connect(to, tolen, timeout);
	
	if (sockfd == 0) {
		return NULL;
	}
	
	if (ldns_tcp_send_query(qbin, sockfd, to, tolen) == 0) {
		return NULL;
	}
	
	answer = ldns_tcp_read_wire(sockfd, answer_size);
	
	close(sockfd);
	
	return answer;
}
