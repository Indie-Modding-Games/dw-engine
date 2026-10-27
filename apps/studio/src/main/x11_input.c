#define _POSIX_C_SOURCE 199309L

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern Bool XTestFakeKeyEvent(Display *display, unsigned int keycode, Bool is_press,
                             unsigned long delay);

static KeySym key_for_action(const char *action) {
  if (strcmp(action, "up") == 0) return XK_Up;
  if (strcmp(action, "down") == 0) return XK_Down;
  if (strcmp(action, "left") == 0) return XK_Left;
  if (strcmp(action, "right") == 0) return XK_Right;
  if (strcmp(action, "confirm") == 0) return XK_Return;
  if (strcmp(action, "cancel") == 0) return XK_Escape;
  if (strcmp(action, "start") == 0) return XK_Tab;
  if (strcmp(action, "ps_confirm") == 0) return XK_k;
  if (strcmp(action, "ps_cancel") == 0) return XK_l;
  if (strcmp(action, "ps_start") == 0) return XK_Return;
  if (strcmp(action, "ps_pause") == 0) return XK_space;
  if (strcmp(action, "ps_menu") == 0) return XK_i;
  if (strcmp(action, "delete") == 0) return XK_Delete;
  if (strcmp(action, "debug_toggle") == 0) return XK_F1;
  fputs("unsupported game input action\n", stderr);
  return NoSymbol;
}

static int send_text(Display *display, Window window, const char *text) {
  const size_t length = strlen(text);
  if (length == 0 || length > 6) {
    fputs("text input must contain between 1 and 6 characters\n", stderr);
    return 2;
  }
  const KeyCode shift = XKeysymToKeycode(display, XK_Shift_L);
  for (size_t index = 0; index < length; ++index) {
    const unsigned char character = (unsigned char)text[index];
    if (!((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
          (character >= '0' && character <= '9') || character == ' ')) {
      fputs("text input only supports ASCII letters, digits and spaces\n", stderr);
      return 2;
    }
    char symbol_name[2] = {character >= 'A' && character <= 'Z'
                                ? (char)(character - 'A' + 'a')
                                : (char)character,
                            '\0'};
    const KeyCode keycode = XKeysymToKeycode(display, character == ' ' ? XK_space
                                                        : XStringToKeysym(symbol_name));
    if (keycode == 0) {
      fputs("a character is unavailable on this keyboard\n", stderr);
      return 1;
    }
    const Bool uppercase = character >= 'A' && character <= 'Z';
    if (uppercase) XTestFakeKeyEvent(display, shift, True, 0);
    XTestFakeKeyEvent(display, keycode, True, 0);
    XFlush(display);
    const struct timespec characterDelay = {.tv_sec = 0, .tv_nsec = 45000000L};
    nanosleep(&characterDelay, NULL);
    XTestFakeKeyEvent(display, keycode, False, 0);
    if (uppercase) XTestFakeKeyEvent(display, shift, False, 0);
    XFlush(display);
    const struct timespec interCharacterDelay = {.tv_sec = 0, .tv_nsec = 45000000L};
    nanosleep(&interCharacterDelay, NULL);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 4) {
    fputs("usage: x11-input <window-id> <action|type> <hold-ms|text>\n", stderr);
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
  const Window window = (Window)id;
  XWindowAttributes attributes;
  if (!XGetWindowAttributes(display, window, &attributes)) {
    fputs("the game window is no longer available\n", stderr);
    XCloseDisplay(display);
    return 1;
  }
  XRaiseWindow(display, window);
  XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
  if (strcmp(argv[2], "type") == 0) {
    const int result = send_text(display, window, argv[3]);
    XCloseDisplay(display);
    return result;
  }
  errno = 0;
  const long hold_ms = strtol(argv[3], &end, 10);
  if (errno != 0 || end == argv[3] || *end != '\0' || hold_ms < 30 || hold_ms > 1500) {
    fputs("hold duration must be between 30 and 1500 ms\n", stderr);
    XCloseDisplay(display);
    return 2;
  }
  const KeySym symbol = key_for_action(argv[2]);
  if (symbol == NoSymbol) { XCloseDisplay(display); return 2; }
  const KeyCode keycode = XKeysymToKeycode(display, symbol);
  if (keycode == 0) {
    fputs("the requested key is unavailable on this keyboard\n", stderr);
    XCloseDisplay(display);
    return 1;
  }

  XTestFakeKeyEvent(display, keycode, True, 0);
  XFlush(display);
  const struct timespec duration = {.tv_sec = hold_ms / 1000,
                                    .tv_nsec = (hold_ms % 1000) * 1000000L};
  nanosleep(&duration, NULL);
  XTestFakeKeyEvent(display, keycode, False, 0);
  XFlush(display);
  XCloseDisplay(display);
  return 0;
}
