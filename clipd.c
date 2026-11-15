/* clipd - minimal clipboard history daemon
 * menü: pkill -USR1 -x clipd
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include "config.h"

static Display *dpy;
static Window win;
static Atom clip, utf8, targets, prop;
static char *ent[MAXENT];
static int n, evbase;
static volatile sig_atomic_t want;
static sigset_t blk, old;

static void onusr(int s) { (void)s; want = 1; }

/* ent[i]'yi başa al */
static void top(int i)
{
	char *s = ent[i];
	memmove(ent + 1, ent, i * sizeof *ent);
	ent[0] = s;
}

static void add(char *s)
{
	int i;
	for (i = 0; i < n; i++)
		if (!strcmp(ent[i], s)) { free(s); top(i); return; }
	if (n == MAXENT) free(ent[--n]);
	ent[n++] = s;
	top(n - 1);
}

static void own(void)
{
	XSetSelectionOwner(dpy, clip, win, CurrentTime);
	XFlush(dpy);
}

static void fetch(XSelectionEvent *e)
{
	Atom type; int fmt; unsigned long len, rest; unsigned char *d = NULL;
	if (e->property == None) return;
	if (XGetWindowProperty(dpy, win, prop, 0, MAXLEN / 4, True, AnyPropertyType,
	                       &type, &fmt, &len, &rest, &d) != Success)
		return;
	if (d && type == utf8 && fmt == 8 && !rest) {
		char *s = strndup((char *)d, len);
		if (s && s[strspn(s, " \t\r\n")]) add(s);
		else free(s);
	}
	if (d) XFree(d);
}

static void serve(XSelectionRequestEvent *r)
{
	XSelectionEvent e = { .type = SelectionNotify, .display = r->display,
		.requestor = r->requestor, .selection = r->selection,
		.target = r->target, .property = r->property ? r->property : r->target,
		.time = r->time };
	if (r->target == targets) {
		Atom t[] = { targets, utf8, XA_STRING };
		XChangeProperty(dpy, r->requestor, e.property, XA_ATOM, 32,
		                PropModeReplace, (unsigned char *)t, 3);
	} else if (r->target == utf8 || r->target == XA_STRING)
		XChangeProperty(dpy, r->requestor, e.property, r->target, 8,
		                PropModeReplace, (unsigned char *)ent[0], strlen(ent[0]));
	else
		e.property = None;
	XSendEvent(dpy, r->requestor, False, 0, (XEvent *)&e);
	XFlush(dpy);
}

/* tek satırlık görünüm */
static void line(const char *s, char *o)
{
	size_t l = 0, i;
	s += strspn(s, " \t\r\n");
	while (s[l] && l < LINE) l++;
	if (s[l]) while (l > 0 && (s[l] & 0xC0) == 0x80) l--; /* UTF-8 ortasında kesme */
	for (i = 0; i < l; i++)
		o[i] = (s[i] == '\n' || s[i] == '\r' || s[i] == '\t') ? ' ' : s[i];
	o[l] = 0;
}

static void menu(void)
{
	char disp[MAXENT][LINE + 1], buf[LINE + 2];
	char *av[] = DMENU;
	int sv[2], i;
	FILE *f;
	pid_t pid;

	if (!n || socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) return;
	if (!(pid = fork())) {
		sigprocmask(SIG_SETMASK, &old, NULL);
		dup2(sv[1], 0); dup2(sv[1], 1);
		close(sv[0]); close(sv[1]);
		execvp(av[0], av);
		_exit(1);
	}
	close(sv[1]);
	for (i = 0; i < n; i++) {
		line(ent[i], disp[i]);
		dprintf(sv[0], "%s\n", disp[i]);
	}
	shutdown(sv[0], SHUT_WR);
	f = fdopen(sv[0], "r");
	if (fgets(buf, sizeof buf, f)) {
		buf[strcspn(buf, "\n")] = 0;
		for (i = 0; i < n; i++)
			if (!strcmp(buf, disp[i])) { top(i); own(); break; }
	}
	fclose(f);
	waitpid(pid, NULL, 0);
	sigtimedwait(&blk, NULL, &(struct timespec){0}); /* menü açıkken gelen sinyali at */
}

int main(void)
{
	struct sigaction sa = { .sa_handler = onusr };
	struct pollfd p;
	XEvent ev;
	int eb;

	if (!(dpy = XOpenDisplay(NULL)) || !XFixesQueryExtension(dpy, &evbase, &eb))
		return 1;
	win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 1, 1, 0, 0, 0);
	clip    = XInternAtom(dpy, "CLIPBOARD", False);
	utf8    = XInternAtom(dpy, "UTF8_STRING", False);
	targets = XInternAtom(dpy, "TARGETS", False);
	prop    = XInternAtom(dpy, "CLIPD_PROP", False);
	XFixesSelectSelectionInput(dpy, DefaultRootWindow(dpy), clip,
	                           XFixesSetSelectionOwnerNotifyMask |
	                           XFixesSelectionWindowDestroyNotifyMask |
	                           XFixesSelectionClientCloseNotifyMask);

	sigaction(SIGUSR1, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);
	sigemptyset(&blk); sigaddset(&blk, SIGUSR1);
	sigprocmask(SIG_BLOCK, &blk, &old);

	XConvertSelection(dpy, clip, utf8, prop, win, CurrentTime); /* mevcut içerik */

	p.fd = ConnectionNumber(dpy); p.events = POLLIN;
	for (;;) {
		while (XPending(dpy)) {
			XNextEvent(dpy, &ev);
			if (ev.type == SelectionNotify)
				fetch(&ev.xselection);
			else if (ev.type == SelectionRequest)
				serve(&ev.xselectionrequest);
			else if (ev.type == evbase + XFixesSelectionNotify) {
				Window o = ((XFixesSelectionNotifyEvent *)&ev)->owner;
				if (o == None) { if (RECLAIM && n) own(); }
				else if (o != win)
					XConvertSelection(dpy, clip, utf8, prop, win, CurrentTime);
			}
		}
		if (want) { want = 0; menu(); }
		else ppoll(&p, 1, NULL, &old); /* sinyal yarışı olmadan bekle */
	}
}
