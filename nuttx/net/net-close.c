/****************************************************************************
 * net/net-close.c
 *
 *   Copyright (C) 2007 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <spudmonkey@racsa.co.cr>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_NET

#include <sys/types.h>
#include <sys/socket.h>

#include <errno.h>
#include <debug.h>

#include <arch/irq.h>
#include <net/uip/uip-arch.h>

#include "net-internal.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_NET_TCP
struct tcp_close_s
{
  FAR struct socket *cl_psock;      /* Reference to the TCP socket */
  sem_t              cl_sem;        /* Semaphore signals disconnect completion */
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Function: netclose_disconnect
 *
 * Description:
 *   Break any current TCP connection
 *
 * Parameters:
 *   conn - uIP TCP connection structure
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from normal user-level logic
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP
static uint8 netclose_interrupt(struct uip_driver_s *dev,
                                struct uip_conn *conn, uint8 flags)
{
  struct tcp_close_s *pstate = (struct tcp_close_s *)conn->data_private;

  nvdbg("flags: %02x\n", flags);

  if (pstate)
    {
      /* UIP_CLOSE: The remote host has closed the connection
       * UIP_ABORT: The remote host has aborted the connection
       */

      if ((flags & (UIP_CLOSE|UIP_ABORT)) != 0)
        {
          /* The disconnection is complete */

          conn->data_flags   = 0;
          conn->data_private = NULL;
          conn->data_event   = NULL;
          sem_post(&pstate->cl_sem);
          nvdbg("Resuming\n");
        }
      else
        {
          /* Drop data received in this state and make sure that UIP_CLOSE
           * is set in the response
           */

          dev->d_len = 0;
          return (flags & ~UIP_NEWDATA) | UIP_CLOSE;
        }
    }

  return flags;
}
#endif /* CONFIG_NET_TCP */

/****************************************************************************
 * Function: netclose_disconnect
 *
 * Description:
 *   Break any current TCP connection
 *
 * Parameters:
 *   conn - uIP TCP connection structure
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   Called from normal user-level logic
 *
 ****************************************************************************/

#ifdef CONFIG_NET_TCP
static inline void netclose_disconnect(FAR struct socket *psock)
{
  struct tcp_close_s state;
  struct uip_conn *conn;
  irqstate_t flags;

  /* Interrupts are disabled here to avoid race conditions */

  flags = irqsave();

  /* Is the TCP socket in a connected state? */

  if (_SS_ISCONNECTED(psock->s_flags))
    {
       /* Set up to receive TCP data events */

       state.cl_psock     = psock;
       sem_init(&state.cl_sem, 0, 0);

       conn               = psock->s_conn;
       conn->data_flags   = UIP_NEWDATA|UIP_CLOSE|UIP_ABORT;
       conn->data_private = (void*)&state;
       conn->data_event   = netclose_interrupt;

       /* Notify the device driver of the availaibilty of TX data */

       netdev_txnotify(&conn->ripaddr);

       /* Wait for the disconnect event */

       (void)sem_wait(&state.cl_sem);

       /* We are now disconnected */

       sem_destroy(&state.cl_sem);
       conn->data_flags   = 0;
       conn->data_private = NULL;
       conn->data_event   = NULL;
    }

  irqrestore(flags);
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Function: net_close
 *
 * Description:
 *   Performs the close operation on socket descriptors
 *
 * Parameters:
 *   sockfd   Socket descriptor of socket
 *
 * Returned Value:
 *   0 on success; -1 on error with errno set appropriately.
 *
 * Assumptions:
 *
 ****************************************************************************/

int net_close(int sockfd)
{
  FAR struct socket *psock = sockfd_socket(sockfd);
  int err;

  /* Verify that the sockfd corresponds to valid, allocated socket */

  if (!psock || psock->s_crefs <= 0)
    {
      err = EBADF;
      goto errout;
    }

  /* Perform uIP side of the close depending on the protocol type */

  switch (psock->s_type)
    {
#ifdef CONFIG_NET_TCP
      case SOCK_STREAM:
        {
          struct uip_conn *conn = psock->s_conn;
          uip_unlisten(conn);          /* No longer accepting connections */
          netclose_disconnect(psock);  /* Break any current connections */
          uip_tcpfree(conn);           /* Free uIP resources */
        }
        break;
#endif

#ifdef CONFIG_NET_UDP
      case SOCK_DGRAM:
        uip_udpfree(psock->s_conn);    /* Free uIP resources */
        break;
#endif

      default:
        err = EBADF;
        goto errout;
    }

  /* Then release the socket structure containing the connection */

  sockfd_release(sockfd);
  return OK;

errout:
  *get_errno_ptr() = err;
  return ERROR;
}

#endif /* CONFIG_NET */
