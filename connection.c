#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

#include "defines.h"
#include "log.h"
#include "connection.h"
#include "id.h"
#include "universe.h"
#include "player.h"
#include "sarray.h"

extern struct universe *univ;

int conn_init(struct conndata *data) {
  int r = 1;
  data->id = gen_id();
  data->rbufs = CONN_BUFSIZE;
  if ((data->rbuf = malloc(data->rbufs)) == NULL) {
    log_printfn("connection", "failed allocating receive buffer for connection %lx", data->id);
    r = 0;
  }
  data->sbufs = CONN_MAXBUFSIZE;
  if ((data->sbuf = malloc(data->sbufs)) == NULL) {
    log_printfn("connection", "failed allocating send buffer for connection %lx", data->id);
    r = 0;
  }
  if (pthread_mutex_init(&data->fd_mutex, NULL) != 0)
    r = 0;
  if (pipe(data->threadfds) != 0)
    r = 0;
  return r;
}

void conn_clean(struct conndata *data) {
  log_printfn("connection", "cleaning connection %lx", data->id);
  if (data->peerfd)
    close(data->peerfd);
  if (data->threadfds[0])
    close(data->threadfds[0]);
  if (data->threadfds[1])
    close(data->threadfds[1]);
  if (&data->fd_mutex != NULL)
    pthread_mutex_destroy(&data->fd_mutex);
  if (data->rbuf)
    free(data->rbuf);
  if (data->sbuf)
    free(data->sbuf);
  if (data->pl)
    player_free(data->pl);
}

void conn_cleanexit(struct conndata *data) {
  conn_clean(data);
  log_printfn("connection", "cleanup complete, terminating thread");
  pthread_exit(0);
}

void conn_send(struct conndata *data, char *format, ...) {
  va_list ap;
  size_t len, sb = 0;
  pthread_mutex_lock(&data->fd_mutex);

  va_start(ap, format);
  vsnprintf(data->sbuf, data->sbufs, format, ap);
  va_end(ap);
  len = strlen(data->sbuf);
  if (len == data->sbufs)
    log_printfn("connection", "warning: sending maximum amount allowable (%d bytes) on connection %lx, this probably indicates overflow", data->sbufs, data->id);
  do {
    sb += send(data->peerfd, data->sbuf + sb, len - sb, MSG_NOSIGNAL);
    if (sb < 1) {
      log_printfn("connection", "send error (connection id %lx), terminating connection", data->id);
      pthread_mutex_unlock(&data->fd_mutex);
      conn_cleanexit(data); // FIXME: What happens if another read is waiting to send data? It will try to access a destroyed mutex ...
    }
  } while (sb < len);

  pthread_mutex_unlock(&data->fd_mutex);
}

void conn_act(struct conndata *data) {
  if (!strcmp(data->rbuf, "help")) {
    conn_send(data, "No help available\n");
  } else if (!strcmp(data->rbuf, "quit")) {
    conn_send(data, "Bye!\n");
    conn_cleanexit(data);
  } else if (!strncmp(data->rbuf, "go ", 3)) {
    data->pl->position = GET_ID((struct sector*)sarray_getbyname(univ->sectors, data->rbuf+3));
    conn_send(data, "Entering %s\n", ((struct sector*)sarray_getbyid(univ->sectors, &data->pl->position))->name);
  }
}

void conn_handlesignal(struct conndata *data, int signal) {
  switch (signal) {
    case MSG_TERM:
      conn_cleanexit(data);
      break;
    default:
      log_printfn("connection", "received unknown signal: %d", signal);
  }
}

void conn_loop(struct conndata *data) {
  int rb, i;
  char* ptr;
  while (1) {
    rb = 0;

    do {
      // FIXME: Doesn't handle CTRL-D for some reason.
      FD_ZERO(&data->rfds);
      FD_SET(data->peerfd, &data->rfds);
      FD_SET(data->threadfds[0], &data->rfds);
      i = select(MAX(data->peerfd, data->threadfds[0]) + 1, &data->rfds, NULL, NULL, NULL);
      if (i == -1) {
	log_printfn("connection", "select() reported error, terminating connection %lx", data->id);
	conn_cleanexit(data);
      }
      if (FD_ISSET(data->threadfds[0], &data->rfds)) {
	// We have received a message on the signalling fd
	read(data->threadfds[0], &i, sizeof(i));
	conn_handlesignal(data, i);
      } else {
	// We have received something from the peer
	rb += recv(data->peerfd, data->rbuf + rb, data->rbufs - rb, 0);
	if (rb < 1) {
	  log_printfn("connection", "peer %s disconected, terminating connection %lx", data->peer, data->id);
	  conn_cleanexit(data);
	}
	if ((rb == data->rbufs) && (data->rbuf[rb - 1] != '\n')) {
	  if (data->rbufs == CONN_MAXBUFSIZE) {
	    log_printfn("connection", "peer sent more data than allowed (%u), connection %lx terminated", CONN_MAXBUFSIZE, data->id);
	    conn_cleanexit(data);
	  }
	  data->rbufs <<= 1;
	  if (data->rbufs > CONN_MAXBUFSIZE)
	    data->rbufs = CONN_MAXBUFSIZE;
	  if ((ptr = realloc(data->rbuf, data->rbufs)) == NULL) {
	    data->rbufs >>= 1;
	    log_printfn("connection", "unable to increase receive buffer size, connection %lx terminated", data->id);
	    conn_cleanexit(data);
	  } else {
	    data->rbuf = ptr;
	  }
	}
      }
    } while (data->rbuf[rb - 1] != '\n');
    
    // Truncate string at EOL (we might have \13\10 or \10)
    if ((rb > 1) && (data->rbuf[rb - 2] == 13)) {
      data->rbuf[rb - 2] = '\0';
    } else {
      data->rbuf[rb - 1] = '\0';
    }

    mprintf("received \"%s\" on socket\n", data->rbuf);

    conn_act(data);

  }
}

void* conn_main(void *dataptr) {
  struct conndata *data = dataptr;
  // temporary solution
  // Create player
  data->pl = malloc(sizeof(struct player));
  data->pl->name = strdup("Alfred");
  data->pl->position = GET_ID(univ->sectors->array);

  mprintf("data is at %p\n", data);
  mprintf("data->peer is at %p\n", data->peer);
  log_printfn("connection", "peer %s successfully logged in as %s", data->peer, data->pl->name);
  conn_loop(data);
  log_printfn("connection", "peer %s disconnected, cleaning up", data->peer);
  conn_cleanexit(data);
  return NULL;
}