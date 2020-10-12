/*
 * BRLTTY - A background process providing access to the console screen (when in
 *          text mode) for a blind person using a refreshable braille display.
 *
 * Copyright (C) 1995-2020 by The BRLTTY Developers.
 *
 * BRLTTY comes with ABSOLUTELY NO WARRANTY.
 *
 * This is free software, placed under the terms of the
 * GNU Lesser General Public License, as published by the Free Software
 * Foundation; either version 2.1 of the License, or (at your option) any
 * later version. Please see the file LICENSE-LGPL for details.
 *
 * Web Page: http://brltty.app/
 *
 * This software is maintained by Dave Mielke <dave@mielke.cc>.
 */

/* InsideVision/braille.c - Braille display library
 * InsideVision InsideONE braille display support
 * Author: Yannick Plassiard <plassiardyannick@gmail.com>
 */

#include "prologue.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>





#ifdef __MINGW32__
#include <ws2tcpip.h>
#include "system_windows.h"
#else /* __MINGW32__ */
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif /* __MINGW32__ */
#include "get_select.h"

#if !defined(AF_LOCAL) && defined(AF_UNIX)
#define AF_LOCAL AF_UNIX
#endif /* !defined(AF_LOCAL) && defined(AF_UNIX) */
 
#if !defined(PF_LOCAL) && defined(PF_UNIX)
#define PF_LOCAL PF_UNIX
#endif /* !defined(PF_LOCAL) && defined(PF_UNIX) */
 
#ifdef WINDOWS
#undef AF_LOCAL
#endif /* WINDOWS */

#ifdef __MINGW32__
#define close(fd) CloseHandle((HANDLE)(fd))
#define LogSocketError(msg) logWindowsSocketError(msg)
#else /* __MINGW32__ */
#define LogSocketError(msg) logSystemError(msg)
#endif /* __MINGW32__ */

#include "log.h"
#include "io_misc.h"
#include "parse.h"
#include "async_wait.h"

#define BRL_HAVE_PACKET_IO
#include "brl_driver.h"
#include "brldefs-iv.h"


#define BRL_IV_BUFSIZE (512)
#define BRL_IV_DEFAULT_PORT (6000)
#define BRL_IV_DEFAULT_HOST ("127.0.0.1")


typedef enum e_iv_status
  {
   STATUS_UNKNOWN = 0,
   STATUS_CONNECTING,
   STATUS_IDENTIFYING,
   STATUS_CONNECTED,
   STATUS_DISCONNECTED
  } t_iv_status;

typedef struct s_iv_data
{
  int socket;
  char inBuf[BRL_IV_BUFSIZE];
  char outBuf[BRL_IV_BUFSIZE];
  int inBufPos;
  int outBufPos;
  t_iv_status status;
} t_iv_data;

typedef struct s_iv_packet {
  uint8_t len;
  uint8_t src;
  uint8_t dst;
  uint8_t command;
  uint8_t* data;
  size_t data_size;
} t_iv_packet;


static t_iv_data *gl_iv_data = NULL;

/** static private method managingnetwork connection */

static int
connectResource(BrailleDisplay *brl,
		const char *device)
{
  int sock = -1;

  gl_iv_data->status = STATUS_CONNECTING;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    logSystemError("socket");
    gl_iv_data->status = STATUS_DISCONNECTED;
    return (0);
  }
  struct sockaddr_in addr;
  addr.sin_port = htons(BRL_IV_DEFAULT_PORT);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(BRL_IV_DEFAULT_HOST);
  socklen_t slen = sizeof(addr);
  if (connect(sock, (struct sockaddr*)&addr, slen) == -1) {
    logSystemError("connect");
    close(sock);
    gl_iv_data->status = STATUS_DISCONNECTED;
    return (0);
  }
  logMessage(LOG_INFO, "Connected to InsideOne's core server.");
  gl_iv_data->socket = sock;
  gl_iv_data->status = STATUS_IDENTIFYING;
  return (1);
}


static void
disconnectResource(BrailleDisplay *brl)
{
  if (gl_iv_data && gl_iv_data->status != STATUS_DISCONNECTED) {
    logMessage(LOG_INFO, "Disconnecting from InsideOne's Core server");
    close(gl_iv_data->socket);
    gl_iv_data->socket = -1;
    gl_iv_data->status = STATUS_DISCONNECTED;
  }
}

static int
pkt_send(BrailleDisplay *brl, void *packet, size_t size) {
  char outPacket[BRL_IV_BUFSIZE];
  size_t realLength = size + 2 + 5;
  if (realLength > 0xFF) {
    logMessage(LOG_ERR, "Packet too log to be sent.");
    return 0;
  }
  t_iv_component src, dst;
  src = IVD_SCREEN_READER;
  dst = IVD_INSIDE_SERVER;
  outPacket[IV_PKT_LEN] = (unsigned char)(realLength);
  outPacket[IV_PKT_SRC] = src;
  outPacket[IV_PKT_DST] = dst;
  memcpy((void *)(outPacket + IV_PKT_CMD), packet, size);
  memcpy((void *)(outPacket + IV_PKT_CMD + size), (void *)("<EOF>"), 5);
  if (realLength + 1 < BRL_IV_BUFSIZE - gl_iv_data->outBufPos) {
    memcpy(gl_iv_data->outBuf + gl_iv_data->outBufPos, outPacket, realLength + 1);
    gl_iv_data->outBufPos += realLength + 1;
  } else {
    logMessage(LOG_ERR, "Failed to add packet: output buffer full");
    return (0);
  }
  return (1);
}


static int
pkt_sendIdentity(BrailleDisplay *brl) {
  char outBuf[1] = { ICC_INIT };
  return pkt_send(brl, outBuf, 1);
}

static int
pkt_sendBus(BrailleDisplay *brl)
{
  char outBuf[BRL_IV_BUFSIZE];
  t_iv_protocol command = ICC_EXPORT_BUS;
  outBuf[0] = command;
  memcpy(outBuf + 1, brl->buffer, brl->textColumns);
  return pkt_send(brl, outBuf, brl->textColumns + 1);
}


static t_iv_packet*
pkt_extract(BrailleDisplay *brl)
{
  if (gl_iv_data != NULL && gl_iv_data->inBufPos > 0) {
    t_iv_packet *pkt = malloc(sizeof(t_iv_packet));
    memcpy(pkt, gl_iv_data->inBuf, 4);
    uint16_t len = pkt->len - 4;
    memcpy(pkt->data, gl_iv_data->inBuf + 4, len);
    return (pkt);
  }
  return NULL;
}


static t_iv_packet*
pkt_read(BrailleDisplay *brl)
{
  char buf[128];
  int ret = -1;

  if (gl_iv_data != NULL && gl_iv_data->status == STATUS_CONNECTED || gl_iv_data->status == STATUS_IDENTIFYING) {
      ret = recv(gl_iv_data->socket, buf, 128, 0);
      if (ret <= 0) {
          logSystemError("recv");
          disconnectResource(brl);
          return (NULL);
      } else {
          memcpy(gl_iv_data->inBuf + gl_iv_data->inBufPos, buf, ret);
          gl_iv_data->inBufPos += ret;
          return (pkt_extract(brl));
      }
  }
  return (NULL);
}


static int
brl_construct (BrailleDisplay *brl, char **parameters, const char *device) {
  if (gl_iv_data) {
    free(gl_iv_data);
    gl_iv_data = NULL;
  }
  if ((gl_iv_data = malloc(sizeof(*gl_iv_data)))) {
    memset(gl_iv_data, 0, sizeof(*gl_iv_data));
    gl_iv_data->socket = -1;
    if (connectResource(brl, device)) {
      if (pkt_sendIdentity(brl)) {
        brl->textColumns = 32;
        brl->textRows = 1;
        return (1);
      }
      disconnectResource(brl);
    }
    free(gl_iv_data);
    gl_iv_data = NULL;
  } else {
    logSystemError("malloc");
  }
  return (0);
}

  
static void
brl_destruct (BrailleDisplay *brl) {
  if (gl_iv_data != NULL) {
    free(gl_iv_data);
    gl_iv_data = NULL;
  }
}

static int
brl_writeWindow (BrailleDisplay *brl, const wchar_t *text) {
  return pkt_sendBus(brl);
}


static int
brl_readCommand (BrailleDisplay *brl, KeyTableCommandContext context) {
    int ret = EOF;
  t_iv_packet *pkt = pkt_read(brl);
  if (pkt) {
    if (pkt->command == ICC_GESTURE) {
        uint8_t zone = (uint8_t)(*(pkt->data));
        uint8_t gesture = (uint8_t)*((pkt->data + 1));
      switch (zone) {
      case IV_KEYGRP_ROUTING_SLIDER_ZONE:
          switch(gesture) {
          case IVG_OneFingerLeft:
              ret = BRL_CMD_FWINLT;
              break;
          case IVG_OneFingerRight:
              ret = BRL_CMD_FWINRT;
              break;
          default:
              break;
          }
          break;
      case IV_KEYGRP_RIGHT_SLIDER_ZONE:
          switch (gesture) {
          case IVG_OneFingerRight:
              ret = BRL_CMD_FWINRT;
              break;
          case IVG_OneFingerLeft:
              ret = BRL_CMD_FWINLT;
              break;
          case IVG_OneFingerUp:
              ret = BRL_CMD_LNUP;
              break;
          case IVG_OneFingerDown:
              ret = BRL_CMD_LNDN;
              break;
          default:
              break;
          }
          break;
      default:
          break;
      }
    }
  }
  return ret;
}

ssize_t
brl_readPacket (BrailleDisplay *brl, void *buffer, size_t length) {
  return 0;
}

static ssize_t
brl_writePacket (BrailleDisplay *brl, const void *packet, size_t length) {
  return (-1);
}

static int
brl_reset (BrailleDisplay *brl) {
  return 0;
}
