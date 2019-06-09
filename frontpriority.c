// frontpriority: Automatically prioritize the process of the active X window
//
// Doesn't need to be run as root as long as you follow the directions below
//
// In order for this to work, your user needs to be able to elevate process
// priority, which can be done by editing /etc/security/limits.conf. Here is
// how to allow just your user to use "nice" levels lower than the default of 0:
// username        -       nice            -10
// or the same, but a user group:
// @groupname      -       nice            -10
//
// You can also do more advanced stuff. For example, set all users (except root)
// to a low priority by default:
// *               -       priority        1
// except for yourself:
// username        -       priority        0
// and then allow your processes to go higher:
// username        -       nice            -10
//
// Source: https://unix.stackexchange.com/q/8983
//
// Should be run in the same X session as the one you'd like the adjustment to
// take place in

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

// Below 3 functions adapted from xdotool
unsigned char *get_window_property(Display *display, Window window, Atom atom,
                                   long *nitems, Atom *type, int *size) {
    Atom actual_type;
    int actual_format;
    unsigned long _nitems;
    unsigned long bytes_after;
    unsigned char *prop;
    int status = XGetWindowProperty(display, window, atom, 0, (~0L),
                                    False, AnyPropertyType, &actual_type,
                                    &actual_format, &_nitems, &bytes_after,
                                    &prop);
    if (status == BadWindow) {
        fprintf(stderr, "Window ID 0x%lx does not exist\n", window);
        return NULL;
    }
    
    if (status != Success) {
        char error[100] = {0};
        XGetErrorText(display, status, error, sizeof(error));
        fprintf(stderr, "XGetWindowProperty on window 0x%lx failed (%s)\n", window, error);
        return NULL;
    }

    if (nitems != NULL) {
        *nitems = _nitems;
    }

    if (type != NULL) {
        *type = actual_type;
    }

    if (size != NULL) {
        *size = actual_format;
    }
    
    return prop;
}

Atom atom_NET_ACTIVE_WINDOW = (Atom)-1;

int get_active_window(Display *display, Window root_window) {
    if (atom_NET_ACTIVE_WINDOW == (Atom)-1) {
        atom_NET_ACTIVE_WINDOW = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    }

    long nitems = 0;
    unsigned char *data = get_window_property(display, root_window, atom_NET_ACTIVE_WINDOW, &nitems, NULL, NULL);

    Window active_window = 0;
    if (nitems > 0) {
        active_window = (Window) *((unsigned long *)data);
    }
    else if (data != NULL) {
        fprintf(stderr, "Could not get active window\n");
    }
    
    free(data);

    return active_window;
}

Atom atom_NET_WM_PID = (Atom)-1;

int get_window_pid(Display *display, Window window) {
    if (atom_NET_WM_PID == (Atom)-1) {
        atom_NET_WM_PID = XInternAtom(display, "_NET_WM_PID", False);
    }

    Atom type;
    int size;
    long nitems = 0;
    unsigned char *data = get_window_property(display, window, atom_NET_WM_PID, &nitems, &type, &size);

    int window_pid = 0;
    if (nitems > 0) {
        window_pid = (int) *((unsigned long *)data);
    }
    else if (data != NULL) {
        fprintf(stderr, "Could not get PID of owner of window 0x%lx\n", window);
    }
    
    free(data);

    return window_pid;
}

int last_pid = 0;
int last_priority = 0;

void reset_last_priority() {
    if (last_pid == 0) {
        // First run, or failed the last time
        return;
    }
    printf("Resetting PID %d to priority %d\n", last_pid, last_priority);
    setpriority(PRIO_PROCESS, last_pid, last_priority);
    last_pid = 0;
}

int priority_change = -1;
enum { ADD, SET } priority_change_setting = ADD;

void handle_window_update(Display *display, Window root_window) {
    reset_last_priority();
    
    Window active_window = get_active_window(display, root_window);
    if (active_window == 0) {
        return;
    }

    int pid = get_window_pid(display, active_window);
    if (pid == 0) {
        return;
    }
    last_pid = pid;
    
    errno = 0;
    last_priority = getpriority(PRIO_PROCESS, pid);
    if (errno != 0) {
        fprintf(stderr, "Failed to get priority of PID %d (%s)\n", pid, strerror(errno));
        return;
    }
    
    int new_priority;
    switch (priority_change_setting) {
    case ADD:
        new_priority = last_priority + priority_change;
        break;
    case SET:
        new_priority = priority_change;
        break;
    default:
        return;
    }
    
    printf("Setting PID %d from priority %d to priority %d\n", pid, last_priority, new_priority);
    if (setpriority(PRIO_PROCESS, pid, new_priority) == -1) {
        fprintf(stderr, "Failed to set priority of PID %d (%s)\n", pid, strerror(errno));
    }
}

void cleanup(int signum) {
    reset_last_priority();
    signal(signum, SIG_DFL);
    raise(signum);
}

int main(int argc, char** argv) {
    char *display_name = NULL;
    Display *display = XOpenDisplay(NULL);
    if (display == NULL) {
        fprintf(stderr, "Unable to open display \"%s\"\n", XDisplayName(display_name));
    }
    int screen = XDefaultScreen(display);
    
    // Since main loop is while (1), set up a cleanup handler
    if (signal(SIGINT, cleanup) == SIG_IGN) {
        signal(SIGINT, SIG_IGN);
    }
    
    Window root_window = XRootWindow(display, screen);
    // Set up priority on the current window
    handle_window_update(display, root_window);
    
    // Monitor the root X window for changes in _NET_ACTIVE_WINDOW
    XSelectInput(display, root_window, PropertyChangeMask);
    while (1) {
        XEvent event;
        XNextEvent(display, &event);
        if (event.type != PropertyNotify) {
            continue;
        }
        XPropertyEvent *property_event = (XPropertyEvent *)&event;
        if (property_event->state != PropertyNewValue || property_event->atom != atom_NET_ACTIVE_WINDOW) {
            continue;
        }
        handle_window_update(display, root_window);
    }
    
    // Unreachable, use signal handler instead
    return 0;
}

