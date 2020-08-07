/*  
 *  Copyright (C) 2020 Jason R. Thorpe.  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *      
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 */

/*
 *  Support for Ethernet tap interfaces.
 *
 *  A single tap instance is used for the entire simulated network.
 *  We treat this as sort of virtual Ethernet switch, with the tap
 *  being the upstream port.  This is very simple, conceptually, and
 *  fits in nicely with the rest of the network simulation model in
 *  GXemul.
 *
 *  Use of the tap interface is completely optional, but if it is used
 *  the all of the virtual IP network support is bypassed completely.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h> 

#include "misc.h"
#include "net.h"

/*
 *  net_tap_rx_for_nic():
 *
 *  Receive a packet from the virtual Ethernet switch for this NIC.
 */
static void net_tap_rx_for_nic(struct net *net, void *extra,
	unsigned char *buf, ssize_t size)
{
	struct ethernet_packet_link *lp;

	/*
	 * XXX IMPLEMENT FILTERING
	 * XXX Requires implementing a common header to NIC data for
	 * XXX us to access here.
	 *
	 * We should deliver to the interface if:
	 *
	 * ==> The packet is broadcast or multicast (the emulated device
	 *     can further apply a multicast filter if it wishes).
	 *  -- or --
	 * ==> The destination MAC address matches the NIC MAC address.
	 *  -- or --
	 *
	 * ==> The interface is in promiscuous mode.
	 */

	lp = net_allocate_ethernet_packet_link(net, extra, (int)size);
	memcpy(lp->data, buf, size);
}

/*
 *  net_tap_rx_avail():
 *
 *  We poll the net-shared tap device and link up any available packets to
 *  their destination interfaces, acting like a virtual Ethernet switch.
 */
void net_tap_rx_avail(struct net *net)
{
	int received_packets_this_tick = 0;
	int max_packets_this_tick = 200;

	for (;;) {
		unsigned char buf[1518];
		ssize_t bytes_read;
		int i;

		if (received_packets_this_tick > max_packets_this_tick)
			break;

		/* Read one packet from the tap device. */
		bytes_read = read(net->tap_fd, buf, sizeof(buf));

		if (bytes_read < 0) {
			/* No more packets available on the tap. */
			break;
		}

		for (i = 0; i < net->n_nics; i++) {
			net_tap_rx_for_nic(net, net->nic_extra[i],
			    buf, bytes_read);
		}
	}
}

/*
 *  net_tap_tx():
 *
 *  Transmit an ethernet packet, as seen from the emulated ethernet controller,
 *  to the net-shared tap device.  Even if the packet is destined only for
 *  a NIC on the local virtual Ethernet switch, we always send it to the
 *  tap device so that the host system can monitor traffic by running tcpdump
 *  on its view of the tap.
 */
void net_tap_tx(struct net *net, void *extra,
	unsigned char *packet, int len)
{
	int i;

	for (i = 0; i < net->n_nics; i++) {
		if (extra == net->nic_extra[i])
			continue;
		net_tap_rx_for_nic(net, net->nic_extra[i], packet, len);
	}

	/*
	 * Don't bother checking for errors here.  The tap driver in the
	 * kernel will either take the entire packet or none of it, and
	 * there isn't any useful error recovery for us anyway.
	 */
	write(net->tap_fd, packet, len);
}

/*
 *  net_tap_init():
 *
 *  Initialize the tap interface.  Returns 1 if successful, 0 otherwise.
 */
int net_tap_init(struct net *net, const char *tapdev)
{
	int fd;
	int one = 1;

	fd = open(tapdev, O_RDWR);
	if (fd < 0) {
		fatal("[ net: unable to open tap device '%s': %s ]\n",
		    tapdev, strerror(errno));
		return 0;
	}

	if (ioctl(fd, FIONBIO, &one) < 0) {
		fatal("[ net: unable to set non-blocking mode on "
		    "tap device '%s': %s ]\n", tapdev, strerror(errno));
		close(fd);
		return 0;
	}

	net->tapdev = strdup(tapdev);
	net->tap_fd = fd;

	return 1;
}
