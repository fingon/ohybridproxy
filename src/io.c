/*
 * $Id: io.c $
 *
 * Author: Markus Stenberg <markus stenberg@iki.fi>
 * Author: Steven Barth <steven@midlink.org>
 *
 * Copyright (c) 2014-2015 cisco Systems, Inc.
 *
 */

#include "io.h"
#include "dns_util.h"

#include <errno.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>

#include <libubox/utils.h>
#include <libubox/ustream.h>

#include "dns_util.h"

static struct list_head active_requests = LIST_HEAD_INIT(active_requests);
extern io_time_t maximum_duration;

void io_reset()
{
  /* Clear all active requests */
  io_request r, nr;
  list_for_each_entry_safe(r, nr, &active_requests, lh)
    io_req_stop(r);
}

bool io_query_start(io_query q)
{
  if (b_query_start(q))
    {
      q->request->running++;
      return true;
    }
  if (!q->request->running)
    b_queries_done(q->request);
  return false;
}

/* Stop processing a query; if it returns false, it killed it's friends too. */
bool io_query_stop(io_query q)
{
  b_query_stop(q);
  if (!(--q->request->running))
    {
      b_queries_done(q->request);
      return false;
    }
  return true;
}

void io_req_init(io_request req)
{
  memset(req, 0, sizeof(*req));
  b_req_init(req);
  INIT_LIST_HEAD(&req->queries);
}

static void _rr_free(io_rr rr)
{
  free(rr->name);
  list_del(&rr->head);
  free(rr);
}

static void _query_free(io_query q)
{
  list_del(&q->head);
  while (!list_empty(&q->rrs))
    _rr_free(list_first_entry(&q->rrs, struct io_rr, head));
  free(q->query);
  free(q);
}

void io_req_free(io_request req)
{
  /* Free shouldn't trigger send. */
  req->sent = true;

  /* Stop sub-queries. */
  io_req_stop(req);

  /* Free contents. */
  while (!list_empty(&req->queries))
    _query_free(list_first_entry(&req->queries, struct io_query, head));

  b_req_free(req);
}


static void _request_timeout(struct uloop_timeout *t)
{
  io_request req = container_of(t, struct io_request, timeout);

  L_DEBUG("timeout");
  io_req_stop(req);
}


void io_req_stop(io_request req)
{
  io_query q;

  if (!req->started)
    return;
  req->started = false;
  list_del(&req->lh);

  /* Cancel the timeout if we already didn't fire it. */
  uloop_timeout_cancel(&req->timeout);

  b_req_stop(req);

  /* Stop the sub-queries. */
  list_for_each_entry(q, &req->queries, head)
    if (!io_query_stop(q))
      return;
}

void io_req_start(io_request req)
{
  io_query q;

  list_add(&req->lh, &active_requests);
  req->started = true;
  if (maximum_duration)
    uloop_timeout_set(&req->timeout, maximum_duration);
  req->timeout.cb = _request_timeout;
  b_req_start(req);
  list_for_each_entry(q, &req->queries, head)
    if (!io_query_start(q))
      return;
}

io_query
io_req_add_query(io_request req, const char *query, uint16_t qtype)
{
  io_query q;

  L_DEBUG("adding query %s/%d to %p", query, qtype, req);
  list_for_each_entry(q, &req->queries, head)
    {
      uint16_t oqtype = q->dq.qtype;
      if (strcmp(q->query, query) == 0
          && (oqtype == qtype
              || oqtype == DNS_SERVICE_ANY))
        {
          L_DEBUG(" .. but it already exists");
          return NULL;
        }
    }
  q = calloc(1, sizeof(*q));
  if (!q)
    return NULL;
  q->query = strdup(query);
  if (!q->query)
    {
      free(q);
      return NULL;
    }
  q->dq.qtype = qtype;
  q->dq.qclass = DNS_CLASS_IN;
  q->request = req;
  INIT_LIST_HEAD(&q->rrs);
  list_add_tail(&q->head, &req->queries);
  return q;
}

io_rr
io_query_add_rr(io_query q, const char *rrname, dns_rr drr, const void *rdata)
{
  io_rr rr = calloc(1, sizeof(*rr) + drr->rdlen);
  if (!rr)
    return NULL;
  if (!(rr->name = strdup(rrname)))
    {
      free(rr);
      return NULL;
    }
  rr->drr = *drr;
  memcpy(rr->drr.rdata, rdata, drr->rdlen);
  list_add(&rr->head, &q->rrs);
  return rr;
}