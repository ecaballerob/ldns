/*
 * dns.h -- defines for the Domain Name System
 *
 * Copyright (c) 2001-2005, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 * This library was created by:
 * Jelte Jansen, Erik Rozendaal and Miek Gieben
 *
 * A bunch of defines that are used in the DNS.
 */

#ifndef _LDNS_DNS_H_
#define _LDNS_DNS_H_

#include <stdio.h>

#include <ldns/util.h>
#include <ldns/buffer.h>
#include <ldns/common.h>
#include <ldns/dname.h>
#include <ldns/dnssec.h>
#include <ldns/error.h>
#include <ldns/higher.h>
#include <ldns/host2str.h>
#include <ldns/host2wire.h>
#include <ldns/net.h>
#include <ldns/packet.h>
#include <ldns/rdata.h>
#include <ldns/resolver.h>
#include <ldns/rr.h>
#include <ldns/str2host.h>
#include <ldns/wire2host.h>
#include <ldns/rr_functions.h>
#include <ldns/keys.h>
#include <ldns/parse.h>
#include <ldns/zone.h>

#define LDNS_IP4ADDRLEN      (32/8)
#define LDNS_IP6ADDRLEN      (128/8)
#define LDNS_PORT	53
#define LDNS_ROOT_LABEL	'\0'
#define LDNS_DEFAULT_TTL	3600

#ifndef LDNS_VERSION
#define LDNS_VERSION	"0.60"
#endif /* LDNS_VERSION */

/* lookup tables for standard DNS stuff  */

/* Taken from RFC 2538, section 2.1.  */
extern ldns_lookup_table ldns_certificate_types[];
/* Taken from RFC 2535, section 7.  */
extern ldns_lookup_table ldns_algorithms[];
/* Taken from RFC 2538.  */
extern ldns_lookup_table ldns_cert_algorithms[];
/* rr types  */
extern ldns_lookup_table ldns_rr_classes[];
/* if these are used elsewhere */
extern ldns_lookup_table ldns_rcodes[];
extern ldns_lookup_table ldns_opcodes[];

#endif /* _LDNS_DNS_H_ */
