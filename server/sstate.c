/* sstate.c - Network UPS Tools server-side state management

   Copyright (C)
	2003	Russell Kroll <rkroll@exploits.org>
	2008	Arjen de Korte <adkorte-guest@alioth.debian.org>
	2012	Arnaud Quette <arnaud.quette@free.fr>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#include "common.h"

#include "timehead.h"

#include "sstate.h"
#include "upstype.h"

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef WIN32
#include <sys/socket.h>
#include <sys/un.h> 
#else
#include <winsock2.h>
#endif

static int parse_args(upstype_t *ups, int numargs, char **arg)
{
	if (numargs < 1)
		return 0;

	if (!strcasecmp(arg[0], "PONG")) {
		upsdebugx(3, "Got PONG from UPS [%s]", ups->name);
		return 1;
	}

	if (!strcasecmp(arg[0], "DUMPDONE")) {
		upsdebugx(3, "UPS [%s]: dump is done", ups->name);
		ups->dumpdone = 1;
		return 1;
	}

	if (!strcasecmp(arg[0], "DATASTALE")) {
		ups->data_ok = 0;
		return 1;
	}

	if (!strcasecmp(arg[0], "DATAOK")) {
		ups->data_ok = 1;
		return 1;
	}

	if (numargs < 2)
		return 0;

	/* FIXME: all these should return their state_...() value! */
	/* ADDCMD <cmdname> */
	if (!strcasecmp(arg[0], "ADDCMD")) {
		state_addcmd(&ups->cmdlist, arg[1]);
		return 1;
	}

	/* DELCMD <cmdname> */
	if (!strcasecmp(arg[0], "DELCMD")) {
		state_delcmd(&ups->cmdlist, arg[1]);
		return 1;
	}

	/* DELINFO <var> */
	if (!strcasecmp(arg[0], "DELINFO")) {
		state_delinfo(&ups->inforoot, arg[1]);
		return 1;
	}

	if (numargs < 3)
		return 0;

	/* SETFLAGS <varname> <flags>... */
	if (!strcasecmp(arg[0], "SETFLAGS")) {
		state_setflags(ups->inforoot, arg[1], numargs - 2, &arg[2]);
		return 1;
	}

	/* SETINFO <varname> <value> */
	if (!strcasecmp(arg[0], "SETINFO")) {
		state_setinfo(&ups->inforoot, arg[1], arg[2]);
		return 1;
	}

	/* ADDENUM <varname> <enumval> */
	if (!strcasecmp(arg[0], "ADDENUM")) {
		state_addenum(ups->inforoot, arg[1], arg[2]);
		return 1;
	}

	/* ADDRANGE <varname> <minvalue> <maxvalue> */
	if (!strcasecmp(arg[0], "ADDRANGE")) {
		state_addrange(ups->inforoot, arg[1], atoi(arg[2]), atoi(arg[3]));
		return 1;
	}

	/* DELENUM <varname> <enumval> */
	if (!strcasecmp(arg[0], "DELENUM")) {
		state_delenum(ups->inforoot, arg[1], arg[2]);
		return 1;
	}

	/* DELRANGE <varname> <minvalue> <maxvalue> */
	if (!strcasecmp(arg[0], "DELRANGE")) {
		state_delrange(ups->inforoot, arg[1], atoi(arg[2]), atoi(arg[3]));
		return 1;
	}

	/* SETAUX <varname> <auxval> */
	if (!strcasecmp(arg[0], "SETAUX")) {
		state_setaux(ups->inforoot, arg[1], arg[2]);
		return 1;
	}

	return 0;
}

/* nothing fancy - just make the driver say something back to us */
static void sendping(upstype_t *ups)
{
	int	ret;
	const char	*cmd = "PING\n";

#ifndef WIN32
	if ((!ups) || (ups->sock_fd < 0)) {
#else
	if ((!ups) || (ups->sock_fd == INVALID_HANDLE_VALUE)) {
#endif
		return;
	}

	upsdebugx(3, "Pinging UPS [%s]", ups->name);

#ifndef WIN32
	ret = write(ups->sock_fd, cmd, strlen(cmd));
#else
	DWORD bytesWritten = 0;
	BOOL  result = FALSE;

	result = WriteFile (ups->sock_fd,cmd,strlen(cmd),&bytesWritten,NULL);
	if( result == 0 ) {
		/* Write failed */
		ret = 0;
	}
	else  {
		ret = (int)bytesWritten;
	}
#endif


	if (ret != (int)strlen(cmd))  {
		upslog_with_errno(LOG_NOTICE, "Send ping to UPS [%s] failed", ups->name);
		sstate_disconnect(ups);
		return;
	}

	time(&ups->last_ping);
}

/* interface */

#ifndef WIN32
int sstate_connect(upstype_t *ups)
{
	int	ret, fd;
	const char	*dumpcmd = "DUMPALL\n";
	struct sockaddr_un	sa;

	memset(&sa, '\0', sizeof(sa));
	sa.sun_family = AF_UNIX;
	snprintf(sa.sun_path, sizeof(sa.sun_path), "%s", ups->fn);

	fd = socket(AF_UNIX, SOCK_STREAM, 0);

	if (fd < 0) {
		upslog_with_errno(LOG_ERR, "Can't create socket for UPS [%s]", ups->name);
		return -1;
	}

	ret = connect(fd, (struct sockaddr *) &sa, sizeof(sa));

	if (ret < 0) {
		time_t	now;

		close(fd);

		/* rate-limit complaints - don't spam the syslog */
		time(&now);
		if (difftime(now, ups->last_connfail) < SS_CONNFAIL_INT)
			return -1;

		ups->last_connfail = now;
		upslog_with_errno(LOG_ERR, "Can't connect to UPS [%s] (%s)", 
			ups->name, ups->fn);

		return -1;
	}

	ret = fcntl(fd, F_GETFL, 0);

	if (ret < 0) {
		upslog_with_errno(LOG_ERR, "fcntl get on UPS [%s] failed", ups->name);
		close(fd);
		return -1;
	}

	ret = fcntl(fd, F_SETFL, ret | O_NDELAY);

	if (ret < 0) {
		upslog_with_errno(LOG_ERR, "fcntl set O_NDELAY on UPS [%s] failed", ups->name);
		close(fd);
		return -1;
	}

	/* get a dump started so we have a fresh set of data */
	ret = write(fd, dumpcmd, strlen(dumpcmd));

	if (ret != (int)strlen(dumpcmd)) {
		upslog_with_errno(LOG_ERR, "Initial write to UPS [%s] failed", ups->name);
		close(fd);
		return -1;
	}

#else
HANDLE sstate_connect(upstype_t *ups)
{
	HANDLE fd;
	char pipename[SMALLBUF];
	const char	*dumpcmd = "DUMPALL\n";
	BOOL  result = FALSE;

	snprintf(pipename, sizeof(pipename), "\\\\.\\pipe\\%s", ups->fn);

	result = WaitNamedPipe(pipename,NMPWAIT_USE_DEFAULT_WAIT);

	if( result == FALSE ) {
		return INVALID_HANDLE_VALUE;
	}

	fd = CreateFile( 
			pipename,   // pipe name 
			GENERIC_READ |  // read and write access 
			GENERIC_WRITE, 
			0,              // no sharing 
			NULL,           // default security attributes FIXME
			OPEN_EXISTING,  // opens existing pipe 
			FILE_FLAG_OVERLAPPED, //  enable async IO
			NULL);          // no template file 

	if (fd == INVALID_HANDLE_VALUE) {
                upslog_with_errno(LOG_ERR, "Can't connect to UPS [%s] (%s)", ups->name, ups->fn);
		return INVALID_HANDLE_VALUE;
        }

	/* get a dump started so we have a fresh set of data */
	DWORD bytesWritten = 0;

	result = WriteFile (fd,dumpcmd,strlen(dumpcmd),&bytesWritten,NULL);
	if (result == 0 || bytesWritten != strlen(dumpcmd)) {
		upslog_with_errno(LOG_ERR, "Initial write to UPS [%s] failed", ups->name);
		CloseHandle(fd);
		return INVALID_HANDLE_VALUE;
	}

	/* Start a read IO so we could wait on the event associated with it */
	ReadFile(fd,ups->buf,sizeof(ups->buf)-1,NULL,&(ups->read_overlapped)); /*-1 to be sure to have a trailling 0 */
#endif

	pconf_init(&ups->sock_ctx, NULL);

	ups->dumpdone = 0;
	ups->stale = 0;

	/* now is the last time we heard something from the driver */
	time(&ups->last_heard);

	/* set ups.status to "WAIT" while waiting for the driver response to dumpcmd */
	state_setinfo(&ups->inforoot, "ups.status", "WAIT");

	upslogx(LOG_INFO, "Connected to UPS [%s]: %s", ups->name, ups->fn);

	return fd;
}

void sstate_disconnect(upstype_t *ups)
{
#ifndef WIN32
	if ((!ups) || (ups->sock_fd < 0)) {
#else
	if ((!ups) || (ups->sock_fd == INVALID_HANDLE_VALUE)) {
#endif
		return;
	}

	sstate_infofree(ups);
	sstate_cmdfree(ups);

	pconf_finish(&ups->sock_ctx);

#ifndef WIN32
	close(ups->sock_fd);
	ups->sock_fd = -1;
#else
	CloseHandle(ups->sock_fd);
	ups->sock_fd = INVALID_HANDLE_VALUE;
#endif
}

void sstate_readline(upstype_t *ups)
{
	int	i, ret;

#ifndef WIN32
	char	buf[SMALLBUF];

	if ((!ups) || (ups->sock_fd < 0)) {
		return;
	}

	ret = read(ups->sock_fd, buf, sizeof(buf));

	if (ret < 0) {
		switch(errno)
		{
		case EINTR:
		case EAGAIN:
			return;

		default:
			upslog_with_errno(LOG_WARNING, "Read from UPS [%s] failed", ups->name);
			sstate_disconnect(ups);
			return;
		}
	}
#else

	if ((!ups) || (ups->sock_fd == INVALID_HANDLE_VALUE)) {
		return;
	}
	char *buf = ups->buf;
	DWORD bytesRead;
	GetOverlappedResult(ups->sock_fd, &ups->read_overlapped, &bytesRead, FALSE);
	ret = bytesRead;
#endif



	for (i = 0; i < ret; i++) {

		switch (pconf_char(&ups->sock_ctx, buf[i]))
		{
		case 1:
			/* set the 'last heard' time to now for later staleness checks */
			if (parse_args(ups, ups->sock_ctx.numargs, ups->sock_ctx.arglist)) {
			        time(&ups->last_heard);
			}
			continue;

		case 0:
			continue;	/* haven't gotten a line yet */

		default:
			/* parse error */
			upslogx(LOG_NOTICE, "Parse error on sock: %s", ups->sock_ctx.errmsg);
			return;
		}
	}

#ifdef WIN32
	/* Restart async read */
	memset(ups->buf,0,sizeof(ups->buf));
	ReadFile( ups->sock_fd, ups->buf, sizeof(ups->buf)-1,NULL, &(ups->read_overlapped)); /* -1 to be sure to have a trailing 0 */
#endif
}

const char *sstate_getinfo(const upstype_t *ups, const char *var)
{
	return state_getinfo(ups->inforoot, var);
}

int sstate_getflags(const upstype_t *ups, const char *var)
{
	return state_getflags(ups->inforoot, var);
}	

int sstate_getaux(const upstype_t *ups, const char *var)
{
	return state_getaux(ups->inforoot, var);
}	

const enum_t *sstate_getenumlist(const upstype_t *ups, const char *var)
{
	return state_getenumlist(ups->inforoot, var);
}

const range_t *sstate_getrangelist(const upstype_t *ups, const char *var)
{
	return state_getrangelist(ups->inforoot, var);
}

const cmdlist_t *sstate_getcmdlist(const upstype_t *ups)
{
	return ups->cmdlist;
}

int sstate_dead(upstype_t *ups, int maxage)
{
	time_t	now;
	double	elapsed;

	/* an unconnected ups is always dead */
#ifndef WIN32
	if (ups->sock_fd < 0) {
#else
	if (ups->sock_fd == INVALID_HANDLE_VALUE) {
#endif
		upsdebugx(3, "sstate_dead: connection to driver socket for UPS [%s] lost", ups->name);
		return 1;	/* dead */
	}

	time(&now);

	/* ignore DATAOK/DATASTALE unless the dump is done */
	if ((ups->dumpdone) && (!ups->data_ok)) {
		upsdebugx(3, "sstate_dead: driver for UPS [%s] says data is stale", ups->name);
		return 1;	/* dead */
	}

	elapsed = difftime(now, ups->last_heard);

	/* somewhere beyond a third of the maximum time - prod it to make it talk */
	if ((elapsed > (maxage / 3)) && (difftime(now, ups->last_ping) > (maxage / 3)))
		sendping(ups);

	if (elapsed > maxage) {
		upsdebugx(3, "sstate_dead: didn't hear from driver for UPS [%s] for %g seconds (max %d)",
					ups->name, elapsed, maxage);
		return 1;	/* dead */
	}

	return 0;
}

/* release all info(tree) data used by <ups> */
void sstate_infofree(upstype_t *ups)
{
	state_infofree(ups->inforoot);

	ups->inforoot = NULL;
}

void sstate_cmdfree(upstype_t *ups)
{
	state_cmdfree(ups->cmdlist);

	ups->cmdlist = NULL;
}

int sstate_sendline(upstype_t *ups, const char *buf)
{
	int	ret;

#ifndef WIN32
	if ((!ups) ||(ups->sock_fd < 0)) {
#else
	if ((!ups) ||(ups->sock_fd == INVALID_HANDLE_VALUE)) {
#endif
		return 0;	/* failed */
	}

#ifndef WIN32
	ret = write(ups->sock_fd, buf, strlen(buf));
#else
	DWORD bytesWritten = 0;
	BOOL  result = FALSE;

	result = WriteFile (ups->sock_fd,buf,strlen(buf),&bytesWritten,NULL);

	if( result == 0 ) {
		ret = 0;
	}
	else  {
		ret = (int)bytesWritten;
	}
#endif

	if (ret == (int)strlen(buf)) {
		return 1;	
	}

	upslog_with_errno(LOG_NOTICE, "Send to UPS [%s] failed", ups->name);
	sstate_disconnect(ups);

	return 0;	/* failed */
}

const st_tree_t *sstate_getnode(const upstype_t *ups, const char *varname)
{
	return state_tree_find(ups->inforoot, varname);
}
