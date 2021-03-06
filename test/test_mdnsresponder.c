/*
 * $Id: test_mdnsresponder.c $
 *
 * Author: Markus Stenberg <markus stenberg@iki.fi>
 *
 * Copyright (c) 2014 cisco Systems, Inc.
 *
 * Created:       Mon Jan  6 13:25:07 2014 mstenber
 * Last modified: Mon Feb 10 07:42:39 2014 mstenber
 * Edit time:     37 min
 *
 */

/* Note: There is no default timeout in mdnsresponder -> we have to
 * add our own to uloop. That shouldn't be a problem in real
 * implementation, skipped here because I'm lazy. */

#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <dns_sd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <libubox/uloop.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

struct dummy_struct {
  struct uloop_fd ufd;
  DNSServiceRef service;
};

/*
 * This is minimalist script which attempts to integrate libubox event
 * loop + mdnsresponder library, and perform a lookup with appropriate
 * timeout.
 */

void dummy_callback(DNSServiceRef service,
                    DNSServiceFlags flags,
                    uint32_t ifindex,
                    DNSServiceErrorType error,
                    const char *name,
                    uint16_t rrtype,
                    uint16_t rrclass,
                    uint16_t rdlen,
                    const void *rdata,
                    uint32_t ttl,
                    void *context)
{
  printf("Callback - flags:%x ifindex:%u error:%d name:%s rrtype/class:%d/%d rrlen:%d ttl:%d\n",
         flags, ifindex, error, name, rrtype, rrclass, rdlen, ttl);
  if (!(flags & kDNSServiceFlagsMoreComing))
    uloop_end();
}

void uloop_fd_callback(struct uloop_fd *u, unsigned int events)
{
  struct dummy_struct *d = (struct dummy_struct *)u;
  printf("uloop_fd_callback %d:%x\n", u->fd, events);
  (void)DNSServiceProcessResult(d->service);
}


int main(int argc, char **argv)
{
  struct dummy_struct ds;
  int c;
  int type = kDNSServiceType_ANY;
  int flags = 0;
  int ifindex = 0;

  memset (&ds, 0, sizeof(ds));
  while ((c = getopt(argc, argv, "f:t:i:")) != -1)
    switch (c)
      {
      case 't':
        type = atoi(optarg);
        break;
      case 'i':
        ifindex = if_nametoindex(optarg);
        break;
      case 'f':
        flags = atoi(optarg);
        break;
      }
  if (argc <= 1)
    {
      fprintf(stderr, "Usage: %s [-f <flags>] [-i <if>] [-t <type>] <name>\n",
              argv[0]);
      exit(1);
    }
  if (DNSServiceQueryRecord(&ds.service, flags, ifindex,
                            argv[1],
                            type,
                            kDNSServiceClass_IN,
                            dummy_callback,
                            NULL) != kDNSServiceErr_NoError)
    {
      fprintf(stderr, "Error initializing DNSServiceQueryRecord\n");
      exit(1);
    }

  if (uloop_init() < 0)
    {
      fprintf(stderr, "Error in uloop_init\n");
      exit(1);
    }
  ds.ufd.fd = DNSServiceRefSockFD(ds.service);
  printf("FD:%d\n", ds.ufd.fd);
  ds.ufd.cb = uloop_fd_callback;
  if (uloop_fd_add(&ds.ufd, ULOOP_READ) < 0)
    {
      fprintf(stderr, "Error in uloop_fd_add\n");
      exit(1);
    }
  printf("Entering event loop\n");
  uloop_run();
  return 0;
}
