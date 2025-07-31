#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

Atom WM_PROTOCOLS;
Atom WM_DELETE_WINDOW;

int x_error_handler(Display* display, XErrorEvent* error) {
    char error_text[1024];
    XGetErrorText(display, error->error_code, error_text, sizeof(error_text));
    std::cerr << "X Error: " << error_text << " (Request: " << (int)error->request_code << ", Minor: " << (int)error->minor_code << ")" << std::endl;
    return 0;
}

void execute_command(const std::vector<std::string>& command_args) {
    pid_t pid = fork();

    if (pid == 0) {
        std::vector<char*> argv_c;
        for (const auto& arg : command_args) {
            argv_c.push_back(const_cast<char*>(arg.c_str()));
        }
        argv_c.push_back(nullptr);

        execvp(argv_c[0], argv_c.data());
        std::cerr << "Error: Could not execute command: " << command_args[0] << std::endl;
        _exit(1);
    } else if (pid > 0) {
    } else {
        std::cerr << "Error: Could not create child process." << std::endl;
    }
}

void close_window(Display* display, Window window) {
    Atom* protocols = nullptr;
    int count;
    if (XGetWMProtocols(display, window, &protocols, &count)) {
        bool delete_supported = false;
        for (int i = 0; i < count; ++i) {
            if (protocols[i] == WM_DELETE_WINDOW) {
                delete_supported = true;
                break;
            }
        }
        XFree(protocols);
        
        if (delete_supported) {
            XClientMessageEvent cm;
            memset(&cm, 0, sizeof(cm));

            cm.type = ClientMessage;
            cm.window = window;
            cm.message_type = WM_PROTOCOLS;
            cm.format = 32;
            cm.data.l[0] = WM_DELETE_WINDOW;
            cm.data.l[1] = CurrentTime;

            XSendEvent(display, window, False, NoEventMask, (XEvent*)&cm);
            std::cout << "Sent close request to window " << window << std::endl;
        } else {
            std::cerr << "Window " << window << " does not support WM_DELETE_WINDOW. Cannot close gracefully." << std::endl;
        }
    } else {
        std::cerr << "Could not get WM_PROTOCOLS for window " << window << ". Cannot close gracefully." << std::endl;
    }
}


int main() {
    Display* display;
    Window root_window;

    display = XOpenDisplay(NULL);
    if (display == NULL) {
        std::cerr << "Could not connect to X server!" << std::endl;
        return 1;
    }
    std::cout << "Connected to X server." << std::endl;

    WM_PROTOCOLS = XInternAtom(display, "WM_PROTOCOLS", False);
    WM_DELETE_WINDOW = XInternAtom(display, "WM_DELETE_WINDOW", False);

    XSetErrorHandler(x_error_handler);

    root_window = DefaultRootWindow(display);
    std::cout << "Root window ID: " << root_window << std::endl;

    XSelectInput(display, root_window, SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask);

    XSetWindowAttributes attributes;
    attributes.event_mask = SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask;

    XGrabServer(display);

    if (!XChangeWindowAttributes(display, root_window, CWEventMask, &attributes)) {
        std::cerr << "Another Window Manager is already running! Cannot become primary WM." << std::endl;
        XUngrabServer(display);
        XCloseDisplay(display);
        return 1;
    }
    XUngrabServer(display);
    std::cout << "Became Window Manager (or attempted to)." << std::endl;

    KeyCode key_a_keycode = XKeysymToKeycode(display, XK_a);
    XGrabKey(display,
             key_a_keycode,
             Mod4Mask,
             root_window,
             True,
             GrabModeAsync,
             GrabModeAsync);
    std::cout << "Grabbed Super + A to open xterm." << std::endl;

    KeyCode key_q_keycode = XKeysymToKeycode(display, XK_q);
    XGrabKey(display,
             key_q_keycode,
             Mod4Mask,
             root_window,
             True,
             GrabModeAsync,
             GrabModeAsync);
    std::cout << "Grabbed Super + Q to close window." << std::endl;


    XEvent event;
    while (true) {
        XNextEvent(display, &event);

        switch (event.type) {
            case CreateNotify:
                std::cout << "CreateNotify event: New window created, ID: " << event.xcreatewindow.window << std::endl;
                
                XTextProperty window_name;
                if (XGetWMName(display, event.xcreatewindow.window, &window_name)) {
                    std::cout << "  Window name: " << (char*)window_name.value << std::endl;
                    XFree(window_name.value);
                }
                
                XSelectInput(display, event.xcreatewindow.window, StructureNotifyMask | ExposureMask | KeyPressMask | ButtonPressMask);
                XMapWindow(display, event.xcreatewindow.window);
                break;

            case MapRequest:
                std::cout << "MapRequest event: Application requests window display ID: " << event.xmaprequest.window << std::endl;
                XMapWindow(display, event.xmaprequest.window);
                break;

            case ConfigureRequest:
                std::cout << "ConfigureRequest event: Window configuration request ID: " << event.xconfigurerequest.window << std::endl;
                XWindowChanges changes;
                changes.x = event.xconfigurerequest.x;
                changes.y = event.xconfigurerequest.y;
                changes.width = event.xconfigurerequest.width;
                changes.height = event.xconfigurerequest.height;
                changes.border_width = event.xconfigurerequest.border_width;
                changes.sibling = event.xconfigurerequest.above;
                changes.stack_mode = event.xconfigurerequest.detail;

                XConfigureWindow(display, event.xconfigurerequest.window, event.xconfigurerequest.value_mask, &changes);
                break;

            case DestroyNotify:
                std::cout << "DestroyNotify event: Window destroyed, ID: " << event.xdestroywindow.window << std::endl;
                break;
            
            case KeyPress:
                if (event.xkey.keycode == key_a_keycode && (event.xkey.state & Mod4Mask)) {
                    std::cout << "Super + A pressed! Opening xterm..." << std::endl;
                    execute_command({"xterm"});
                } else if (event.xkey.keycode == key_q_keycode && (event.xkey.state & Mod4Mask)) {
                    std::cout << "Super + Q pressed! Closing window..." << std::endl;
                    Window focused_window;
                    int revert_to;
                    XGetInputFocus(display, &focused_window, &revert_to);
                    if (focused_window != None && focused_window != root_window) {
                        close_window(display, focused_window);
                    } else {
                        std::cout << "No window to close or root window is focused." << std::endl;
                    }
                }
                break;

            default:
                break;
        }
    }

    XCloseDisplay(display);
    return 0;
}