/*
** webcandc: stand-in for the Greenleaf CommLib header <modem.h>.
**
** Greenleaf was never released and serial/modem play has no browser
** equivalent. The modem code is compiled out (WEBCANDC_NO_NET); this header
** only supplies the types that always-included game headers mention.
*/
#ifndef WEBCANDC_MODEM_H
#define WEBCANDC_MODEM_H

typedef struct webcandc_greenleaf_port PORT;

#define ASSUCCESS 0
#define PORTBUF_MAX 80

#endif
