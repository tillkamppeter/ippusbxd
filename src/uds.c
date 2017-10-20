/* Copyright (C) 2017 Daniel Dressler and contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "http.h"
#include "logging.h"
#include "options.h"
#include "uds.h"

struct uds_sock_t *uds_open(const char *path) {
  struct uds_sock_t *sock = calloc(1, sizeof(*sock));
  NOTE("Unlinking %s", path);
  unlink(path);
  if (sock == NULL) {
    ERR("UDS: Allocating memory for socket failed");
    goto error;
  }

  if ((sock->fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
    ERR("UDS: Opening socket failed");
    goto error;
  }

  int val = 1;
  if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val)) < 0) {
    ERR("UDS: Setting socket options failed");
    goto error;
  }

  // Configure socket parameters.
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));

  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, path);

  NOTE("UDS: Binding to %s", path);

  if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr))) {
    ERR("UDS: Binding to socket failed - %s", strerror(errno));
    goto error;
  }

  if (listen(sock->fd, HTTP_MAX_PENDING_CONNS)) {
    ERR("UDS: Listen on socket failed");
    goto error;
  }

  sock->addr = addr;

  return sock;

error:
  if (sock != NULL) {
    if (sock->fd != -1) {
      close(sock->fd);
    }
    free(sock);
  }
  return NULL;
}

void uds_close(struct uds_sock_t *sock) {
  close(sock->fd);
  unlink(sock->addr.sun_path);
  free(sock);
}

struct uds_conn_t *uds_connect(struct uds_sock_t *sock) {
  struct uds_conn_t *conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    ERR("UDS: Allocationg memory for connection failed");
    goto error;
  }

  if (sock == NULL) {
    ERR("UDS: No valid unix socket provided");
    goto error;
  }

  fd_set rdfs;
  FD_ZERO(&rdfs);
  FD_SET(sock->fd, &rdfs);
  int nfds = sock->fd + 1;
  int retval = select(nfds, &rdfs, NULL, NULL, NULL);

  if (g_options.terminate)
    goto error;

  if (retval < 1) {
    ERR("Failed to open uds connection");
    goto error;
  }

  if (sock && FD_ISSET(sock->fd, &rdfs)) {
    if ((conn->fd = accept(sock->fd, NULL, NULL)) < 0) {
      ERR("UDS: Accepting connection failed");
      goto error;
    }
  } else {
    ERR("Select failed");
    goto error;
  }

  return conn;

error:
  if (conn != NULL)
    free(conn);
  return NULL;
}

void uds_conn_close(struct uds_conn_t *conn) {
  shutdown(conn->fd, SHUT_RDWR);
  close(conn->fd);
  free(conn);
}

struct http_packet_t *uds_packet_get(struct uds_conn_t *conn,
                                     struct http_message_t *msg) {
  struct http_packet_t *pkt = packet_new(msg);
  if (pkt == NULL) {
    ERR("UDS: Allocating memory for incoming uds message failed");
    goto error;
  }

  size_t want_size = packet_pending_bytes(pkt);
  if (want_size == 0) {
    NOTE("UDS: Got %lu from spare buffer", pkt->filled_size);
    return pkt;
  }

  struct timeval tv;
  tv.tv_sec = 3;
  tv.tv_usec = 0;
  if (setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv)) <
      0) {
    ERR("UDS: Setting options for connection socket failed");
    goto error;
  }

  while (want_size != 0 && !msg->is_completed && !g_options.terminate) {
    NOTE("UDS: Getting %d bytes", want_size);
    uint8_t *subbuffer = pkt->buffer + pkt->filled_size;
    ssize_t gotten_size = recv(conn->fd, subbuffer, want_size, 0);

    if (gotten_size < 0) {
      int errno_saved = errno;
      ERR("UDS: recv failed with err %d:%s", errno_saved,
          strerror(errno_saved));
      conn->is_closed = 1;
      goto error;
    }

    NOTE("UDS: Got %d bytes", gotten_size);
    if (gotten_size == 0) {
      conn->is_closed = 1;
      if (pkt->filled_size == 0) {
        // Client closed connection.
        goto error;
      } else {
        break;
      }
    }

    packet_mark_received(pkt, (unsigned)gotten_size);
    want_size = packet_pending_bytes(pkt);
    NOTE("UDS: Want %d more bytes; Message %scompleted", want_size,
         msg->is_completed ? "" : "not ");
  }

  NOTE("UDS: Received %lu bytes", pkt->filled_size);
  return pkt;

error:
  if (pkt != NULL) packet_free(pkt);
  return NULL;
}

int uds_packet_send(struct uds_conn_t *conn, struct http_packet_t *pkt) {
  size_t remaining = pkt->filled_size;
  size_t total = 0;

  while (remaining > 0 && !g_options.terminate) {
    ssize_t sent = send(conn->fd, pkt->buffer + total, remaining, MSG_NOSIGNAL);

    if (sent < 0) {
      if (errno == EPIPE) {
        conn->is_closed = 1;
        return 0;
      }
      ERR("Failed to send data over UDS");
      return -1;
    }

    size_t sent_ulong = (unsigned)sent;
    total += sent_ulong;
    if (sent_ulong >= remaining)
      remaining = 0;
    else
      remaining -= sent_ulong;
  }

  NOTE("UDS: sent %lu bytes", total);
  return 0;
}
