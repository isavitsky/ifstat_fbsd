/*
 * Network speed application for FreeBSD.
 * Use in conjunction with 'Command Output'
 * KDE applet: https://github.com/Zren/plasma-applet-commandoutput
 *
 * Based on https://github.com/lcdproc/lcdproc
 * machine_FreeBSD.c: machine_get_iface_stats()
 *
 * Copyright (c) 2003 Thomas Runge (coto@core.de)
 *
 * All rights reserved.
 *
 * isavytskyi
 * 2023-07
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sysexits.h>
#include <sys/sysctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/user.h>
#include <err.h>
#include <net/if.h>
#include <net/if_mib.h>

#ifndef TRUE
# define TRUE    1
#endif
#ifndef FALSE
# define FALSE   0
#endif

#define SHM_NPREFIX "/traffic_"

/** Status definitions for network interfaces */
typedef enum {
	down = 0,
	up = 1,
} IfaceStatus;

/* Network Interface information */
typedef struct iface_info
{
	char *name;		/**< physical interface name */
	char *alias;		/**< displayed name of interface */

	IfaceStatus status;	/**< status of the interface */

	time_t last_online;

	double rc_byte;		/**< currently received bytes */
	double rc_byte_old;	/**< previously received bytes */

	double tr_byte;		/**< currently sent bytes */
	double tr_byte_old;	/**< previously sent bytes */

	double rc_pkt;		/**< currently received packages */
	double rc_pkt_old;	/**< previously received packages */

	double tr_pkt;		/**< currently sent packages */
	double tr_pkt_old;	/**< previously sent packages */
} IfaceInfo;

IfaceInfo iface;

int
machine_get_iface_stats(IfaceInfo * interface)
{
	int rows;
	int name[6] = {CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_IFDATA, 0, IFDATA_GENERAL};
	size_t len;
	struct ifmibdata ifmd;	/* ifmibdata contains the network statistics */

	len = sizeof(rows);
	/* get number of interfaces */
	if (sysctlbyname("net.link.generic.system.ifcount", &rows, &len, NULL, 0) == 0) {
		interface->status = down;	/* set status down by default */

		len = sizeof(ifmd);
		/*
		 * walk through all interfaces in the ifmib table from last
		 * to first
		 */
		for (; rows > 0; rows--) {
			name[4] = rows;	/* set the interface index */
			/* retrive the ifmibdata for the current index */
			if (sysctl(name, 6, &ifmd, &len, NULL, 0) == -1) {
				perror("read sysctl");
				break;
			}
			/* check if its interface name matches */
			if (strcmp(ifmd.ifmd_name, interface->name) == 0) {

				interface->rc_byte = ifmd.ifmd_data.ifi_ibytes;
				interface->tr_byte = ifmd.ifmd_data.ifi_obytes;
				interface->rc_pkt = ifmd.ifmd_data.ifi_ipackets;
				interface->tr_pkt = ifmd.ifmd_data.ifi_opackets;

				if (interface->last_online == 0) {
					interface->rc_byte_old = interface->rc_byte;
					interface->tr_byte_old = interface->tr_byte;
					interface->rc_pkt_old = interface->rc_pkt;
					interface->tr_pkt_old = interface->tr_pkt;
				}

				if ((ifmd.ifmd_flags & IFF_UP) == IFF_UP) {
					interface->status = up;	/* is up */
					interface->last_online = time(NULL);	/* save actual time */
				}

				return (TRUE);
			}
		}
		/* if we are here there is no interface with the given name */
		return (TRUE);
	}
	else {
		perror("read sysctlbyname");
		return (FALSE);
	}
}

void
print_speed(double timestamp_old_s, double rx_bytes, double tx_bytes) {
	double bps;
	double interval_s;
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv,&tz);

	interval_s = tv.tv_sec + tv.tv_usec / 1e6 - timestamp_old_s;
	if ( interval_s < 1.0e-6 )
		interval_s = 1.0;

	bps = rx_bytes * 8 / interval_s;
	printf("∇ ");
	if ( bps < 1000.0 ) {
		printf("%5d b\n", (int)(bps));
	} else if ( bps < 1.0e6 ) {
		bps /= 1000.0;
		printf("%5.1f k\n", bps);
	} else if ( bps < 1.0e9 ) {
		bps /= 1.0e6;
		printf("%5.1f M\n", bps);
	} else {
		bps /= 1.0e9;
		printf("%5.1f G\n", bps);
	}

	bps = tx_bytes * 8 / interval_s;
	printf("∆ ");
	if ( bps < 1000.0 ) {
		printf("%5d b\n", (int)(bps));
	} else if ( bps < 1.0e6 ) {
		bps /= 1000.0;
		printf("%5.1f k\n", bps);
	} else if ( bps < 1.0e9 ) {
		bps /= 1.0e6;
		printf("%5.1f M\n", bps);
	} else {
		bps /= 1.0e9;
		printf("%5.1f G\n", bps);
	}
}

/* get_traffic_speed() uses shared memory for temporary data storage */
int
get_traffic_speed(IfaceInfo iface) {
	const char shm_prefix[] = SHM_NPREFIX;
	char shm_name[NAME_MAX];
	char buffer[getpagesize()];
	char stmp[NAME_MAX];
	ssize_t len;
	int fd;
	double rx_bytes, rx_bold = 0.0;
	double tx_bytes, tx_bold = 0.0;
	double timestamp_s = 0.0;
	double timestamp_s_old = 0.0;
	struct timeval tv;
	struct timezone tz;

	gettimeofday(&tv,&tz);
	timestamp_s = tv.tv_sec + tv.tv_usec / 1e6;
	if ( iface.name == NULL ) {
		print_speed(timestamp_s, 0, 0);
		return 1;
	}

	/* Shared memory 'file' path consists of SHM_PREFIX,
	 * interface name, user name and XDG_SESSION_COOKIE when available.
	 */
	memset(&shm_name, 0, NAME_MAX);
	strncpy(shm_name, shm_prefix, sizeof(shm_prefix) +1);
	strncat(shm_name, iface.name, sizeof(shm_name) - strlen(shm_name) -1);
	strncat(shm_name, "_", sizeof(shm_name) - strlen(shm_name) -1);
	if ( getenv("USER") != NULL ) {
		strncat(shm_name, getenv("USER"), sizeof(shm_name) - strlen(shm_name) -1);
	}
	if ( getenv("XDG_SESSION_COOKIE") != NULL ) {
		strncat(shm_name, "_", sizeof(shm_name) - strlen(shm_name) -1);
		strncat(shm_name, getenv("XDG_SESSION_COOKIE"), sizeof(shm_name) - strlen(shm_name) -1);
	}

	//printf("shm_name: %s\n", shm_name);
	//shm_unlink(shm_name);
	//return 1;

	fd = shm_open(shm_name, O_RDWR | O_CREAT, 0600);
	if (fd < 0) {
		print_speed(timestamp_s, 0, 0);
		err(EX_OSERR, "%s: shm_open", __func__);
	}

	memset(&buffer, 0, sizeof(buffer));
	len = pread(fd, buffer, sizeof(buffer), 0);
	if ( len <= 0 ) {
		if (ftruncate(fd, sizeof(buffer)) <	0) {
			print_speed(timestamp_s, 0, 0);
			err(EX_IOERR, "%s: ftruncate", __func__);
		}
	} else {
		uint8_t n;

		n = 1;
		char* token = strtok(buffer, ":");
		while ( token != NULL ) {
			if ( n == 1 )
				timestamp_s_old = atof(token);
			if ( n == 2 )
				rx_bold = atof(token);
			if ( n == 3 )
				tx_bold = atof(token);
			n++;
			token = strtok(NULL, ":");
		}
	}

	/* data buffer: <timestamp, seconds>:<iface RX bytes>:<iface TX bytes> */
	memset(&buffer, 0, sizeof(buffer));
	// timestamp
	memset(&stmp, 0, sizeof(stmp));
	snprintf(stmp, sizeof(stmp), "%f", timestamp_s);
	strncpy(buffer, stmp, sizeof(buffer));
	strncat(buffer, ":", sizeof(buffer) - strlen(buffer) -1);
	// iface rx byte counter
	memset(&stmp, 0, sizeof(stmp));
	snprintf(stmp, sizeof(stmp), "%f", iface.rc_byte);
	strncat(buffer, stmp, sizeof(buffer) - strlen(buffer) -1);
	strncat(buffer, ":", sizeof(buffer) - strlen(buffer) -1);
	// iface tx byte counter
	memset(&stmp, 0, sizeof(stmp));
	snprintf(stmp, sizeof(stmp), "%f", iface.tr_byte);
	strncat(buffer, stmp, sizeof(buffer) - strlen(buffer) -1);

	len = pwrite(fd, buffer, getpagesize(), 0);
	if (len < 0) {
		print_speed(timestamp_s, 0, 0);
		err(EX_IOERR, "%s: pwrite", __func__);
	}

	rx_bytes = iface.rc_byte - rx_bold;
	tx_bytes = iface.tr_byte - tx_bold;
	print_speed(timestamp_s_old, rx_bytes, tx_bytes);
	return 0;
}

int
main(int argc, char **argv) {

	if ( argc < 2 ) {
		fprintf(stderr, "FreeBSD network speed app for use in conjunction with Command Output KDE applet.\n");
		fprintf(stderr, "Usage: %s <netowrk interface name>\n", argv[0]);
		return 1;
	}


	iface.name=argv[1];
	machine_get_iface_stats(&iface);
	return get_traffic_speed(iface);
}

