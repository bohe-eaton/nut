#ifndef SERIAL_H_SEEN
#define SERIAL_H_SEEN 1

#include "attribute.h"

#include "config.h"

#if defined(HAVE_SYS_TERMIOS_H)
#  include <sys/termios.h>      /* for speed_t */
#else
#ifndef WIN32
#  include <termios.h>
#else
#define speed_t DWORD
#define B300 CBR_300
#define B600 CBR_600
#define B1200 CBR_1200
#define B2400 CBR_2400
#define B4800 CBR_4800
#define B9600 CBR_9600
#define B19200 CBR_19200
#define B38400 CBR_38400
#endif
#endif /* HAVE_SYS_TERMIOS_H */

/* limit the amount of spew that goes in the syslog when we lose the UPS */
#define SER_ERR_LIMIT 10	/* start limiting after 10 in a row  */
#define SER_ERR_RATE 100	/* then only print every 100th error */

#ifndef WIN32
int ser_open_nf(const char *port);
int ser_open(const char *port);

int ser_set_speed_nf(int fd, const char *port, speed_t speed);
int ser_set_speed(int fd, const char *port, speed_t speed);

/* set the state of modem control lines */
int ser_set_dtr(int fd, int state);
int ser_set_rts(int fd, int state);

/* get the status of modem control lines */
int ser_get_dsr(int fd);
int ser_get_cts(int fd);
int ser_get_dcd(int fd);

int ser_flush_io(int fd);

int ser_close(int fd, const char *port);

#else
#include <windows.h>
HANDLE ser_open(const char *port);
int ser_set_speed(HANDLE fd, const char *port, speed_t speed);

/* set the state of modem control lines */
int ser_set_dtr(HANDLE fd, int state);
int ser_set_rts(HANDLE fd, int state);

/* get the status of modem control lines */
int ser_get_dsr(HANDLE);
int ser_get_cts(HANDLE);
int ser_get_dcd(HANDLE);

int ser_flush_io(HANDLE);

int ser_close(HANDLE fd, const char *port);

#endif

#ifndef WIN32
int ser_send_char(int fd, unsigned char ch);
#else
int ser_send_char(HANDLE fd, unsigned char ch);
#endif

/* send the results of the format string with d_usec delay after each char */
#ifndef WIN32
int ser_send_pace(int fd, unsigned long d_usec, const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 3, 4)));
#else
int ser_send_pace(HANDLE fd, unsigned long d_usec, const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 3, 4)));
#endif

/* send the results of the format string with no delay */
#ifndef WIN32
int ser_send(int fd, const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));
#else
int ser_send(HANDLE fd, const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));
#endif

/* send buflen bytes from buf with no delay */
#ifndef WIN32
int ser_send_buf(int fd, const void *buf, size_t buflen);
#else
int ser_send_buf(HANDLE fd, const void *buf, size_t buflen);
#endif

/* send buflen bytes from buf with d_usec delay after each char */
#ifndef WIN32
int ser_send_buf_pace(int fd, unsigned long d_usec, const void *buf, 
	size_t buflen);
#else
int ser_send_buf_pace(HANDLE fd, unsigned long d_usec, const void *buf, 
	size_t buflen);
#endif

#ifndef WIN32
int ser_get_char(int fd, void *ch, long d_sec, long d_usec);
#else
int ser_get_char(HANDLE fd, void *ch, long d_sec, long d_usec);
#endif

#ifndef WIN32
int ser_get_buf(int fd, void *buf, size_t buflen, long d_sec, long d_usec);
#else
int ser_get_buf(HANDLE fd, void *buf, size_t buflen, long d_sec, long d_usec);
#endif

/* keep reading until buflen bytes are received or a timeout occurs */
#ifndef WIN32
int ser_get_buf_len(int fd, void *buf, size_t buflen, long d_sec, long d_usec);
#else
int ser_get_buf_len(HANDLE fd, void *buf, size_t buflen, long d_sec, long d_usec);
#endif

/* reads a line up to <endchar>, discarding anything else that may follow,
   with callouts to the handler if anything matches the alertset */
#ifndef WIN32
int ser_get_line_alert(int fd, void *buf, size_t buflen, char endchar,
	const char *ignset, const char *alertset, void handler (char ch), 
	long d_sec, long d_usec);
#else
int ser_get_line_alert(HANDLE fd, void *buf, size_t buflen, char endchar,
	const char *ignset, const char *alertset, void handler (char ch), 
	long d_sec, long d_usec);
#endif

/* as above, only with no alertset handling (just a wrapper) */
#ifndef WIN32
int ser_get_line(int fd, void *buf, size_t buflen, char endchar,
	const char *ignset, long d_sec, long d_usec);
#else
int ser_get_line(HANDLE fd, void *buf, size_t buflen, char endchar,
	const char *ignset, long d_sec, long d_usec);
#endif

#ifndef WIN32
int ser_flush_in(int fd, const char *ignset, int verbose);
#else
int ser_flush_in(HANDLE fd, const char *ignset, int verbose);
#endif

/* unified failure reporting: call these often */
void ser_comm_fail(const char *fmt, ...)
	__attribute__ ((__format__ (__printf__, 1, 2)));
void ser_comm_good(void);


#endif	/* SERIAL_H_SEEN */
