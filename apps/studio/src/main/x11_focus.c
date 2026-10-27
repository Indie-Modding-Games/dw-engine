#include <X11/Xlib.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  if (argc != 2) {
    fputs("usage: x11-focus <window-id>\n", stderr);
    return 2;
  }

  errno = 0;
  char *end = NULL;
  const unsigned long id = strtoul(argv[1], &end, 0);
  if (errno != 0 || end == argv[1] || *end != '\0' || id == 0) {
    fputs("invalid X11 window id\n", stderr);
    return 2;
  }

  Display *display = XOpenDisplay(NULL);
  if (display == NULL) {
    fputs("cannot open the X11 display\n", stderr);
    return 1;
  }

  XWindowAttributes attributes;
  if (!XGetWindowAttributes(display, (Window)id, &attributes)) {
    fputs("the game window is no longer available\n", stderr);
    XCloseDisplay(display);
    return 1;
  }

  XSetInputFocus(display, (Window)id, RevertToPointerRoot, CurrentTime);
  XFlush(display);
  XCloseDisplay(display);
  return 0;
}
