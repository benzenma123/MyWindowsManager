#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <algorithm>

// Các biến toàn cục
Atom WM_PROTOCOLS;
Atom WM_DELETE_WINDOW;
Atom NET_WM_NAME;
Atom UTF8_STRING;
// Các biến để quản lý việc di chuyển cửa sổ
bool is_moving = false;
int start_x, start_y;
int start_win_x, start_win_y;
Window current_moving_window = None;

// Các biến để quản lý layout và cửa sổ
std::vector<Window> managed_windows;
const float master_ratio = 0.6; // Cửa sổ chính chiếm 60% màn hình
const int border_width = 2; // Chiều rộng viền cửa sổ
const int statusbar_height = 20; // Chiều cao của thanh taskbar
unsigned long focused_border_color;
unsigned long unfocused_border_color;
Window focused_window = None;
Window statusbar_window = None;

// Xử lý lỗi X
int x_error_handler(Display* display, XErrorEvent* error) {
    char error_text[1024];
    XGetErrorText(display, error->error_code, error_text, sizeof(error_text));
    std::cerr << "X Error: " << error_text << " (Request: " << (int)error->request_code << ", Minor: " << (int)error->minor_code << ")" << std::endl;
    return 0;
}

// Chạy một lệnh trong một process con mới
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
        // Parent process
    } else {
        std::cerr << "Error: Could not create child process." << std::endl;
    }
}

// Gửi yêu cầu đóng cửa sổ một cách lịch sự
void close_window(Display* display, Window window) {
    Atom* protocols = nullptr;
    int count;
    bool delete_supported = false;

    if (XGetWMProtocols(display, window, &protocols, &count)) {
        for (int i = 0; i < count; ++i) {
            if (protocols[i] == WM_DELETE_WINDOW) {
                delete_supported = true;
                break;
            }
        }
        XFree(protocols);
    }
    
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
        std::cout << "Sent polite close request to window " << window << std::endl;
    } else {
        std::cerr << "Window " << window << " does not support WM_DELETE_WINDOW. Attempting forceful kill." << std::endl;
        XKillClient(display, window);
    }
}

// Hàm tiling chính
void tile_windows(Display* display, Window root_window) {
    if (managed_windows.empty()) return;

    XWindowAttributes root_attrs;
    XGetWindowAttributes(display, root_window, &root_attrs);
    
    const int num_windows = managed_windows.size();
    const int screen_width = root_attrs.width;
    const int screen_height = root_attrs.height;
    
    if (num_windows == 1) {
        XMoveResizeWindow(display, managed_windows[0], 0, statusbar_height, screen_width - 2*border_width, screen_height - statusbar_height - 2*border_width);
        return;
    }
    
    const int master_width = screen_width * master_ratio;
    XMoveResizeWindow(display, managed_windows[0], 0, statusbar_height, master_width - 2*border_width, screen_height - statusbar_height - 2*border_width);

    const int stack_width = screen_width - master_width;
    const int stack_height = (screen_height - statusbar_height) / (num_windows - 1);
    
    for (int i = 1; i < num_windows; ++i) {
        XMoveResizeWindow(display, managed_windows[i], 
                          master_width, 
                          (i - 1) * stack_height + statusbar_height,
                          stack_width - 2*border_width,
                          stack_height - 2*border_width);
    }
}

// Hàm đặt màu viền cho cửa sổ
void set_window_border(Display* display, Window window, bool is_focused) {
    if (is_focused) {
        XSetWindowBorder(display, window, focused_border_color);
    } else {
        XSetWindowBorder(display, window, unfocused_border_color);
    }
}

// Hàm vẽ lại thanh taskbar
void draw_statusbar(Display* display) {
    XClearWindow(display, statusbar_window);
    
    XWindowAttributes attrs;
    XGetWindowAttributes(display, statusbar_window, &attrs);
    
    XGCValues gcv;
    GC gc = XCreateGC(display, statusbar_window, 0, &gcv);
    XSetForeground(display, gc, XWhitePixel(display, DefaultScreen(display)));
    
    // Lấy thông tin từ thuộc tính _NET_WM_NAME của cửa sổ gốc
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char* prop;
    
    if (XGetWindowProperty(display, DefaultRootWindow(display), NET_WM_NAME, 0, 1024, False, UTF8_STRING, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success && nitems > 0) {
        XDrawString(display, statusbar_window, gc, 5, 15, (const char*)prop, nitems);
        XFree(prop);
    }
    XFreeGC(display, gc);
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
    NET_WM_NAME = XInternAtom(display, "_NET_WM_NAME", False);
    UTF8_STRING = XInternAtom(display, "UTF8_STRING", False);

    XSetErrorHandler(x_error_handler);

    root_window = DefaultRootWindow(display);
    std::cout << "Root window ID: " << root_window << std::endl;

    focused_border_color = XWhitePixel(display, DefaultScreen(display));
    unfocused_border_color = XBlackPixel(display, DefaultScreen(display));

    XSelectInput(display, root_window, SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask | ButtonPressMask | EnterWindowMask | PropertyChangeMask);

    XSetWindowAttributes attributes;
    attributes.event_mask = SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask | ButtonPressMask | EnterWindowMask | PropertyChangeMask;
    attributes.border_pixel = unfocused_border_color;
    
    // Tạo cửa sổ taskbar
    XWindowAttributes root_attrs;
    XGetWindowAttributes(display, root_window, &root_attrs);
    statusbar_window = XCreateSimpleWindow(display, root_window, 0, 0, root_attrs.width, statusbar_height, 0,
                                           XBlackPixel(display, DefaultScreen(display)), XBlackPixel(display, DefaultScreen(display)));
    XMapWindow(display, statusbar_window);
    
    // Lắng nghe các sự kiện cần thiết trên cửa sổ taskbar
    XSelectInput(display, statusbar_window, ExposureMask);
    
    XGrabServer(display);
    if (!XChangeWindowAttributes(display, root_window, CWEventMask, &attributes)) {
        std::cerr << "Another Window Manager is already running! Cannot become primary WM." << std::endl;
        XUngrabServer(display);
        XCloseDisplay(display);
        return 1;
    }
    XUngrabServer(display);
    std::cout << "Became Window Manager (or attempted to)." << std::endl;
    
    Window* children;
    unsigned int n_children;
    XQueryTree(display, root_window, &root_window, &root_window, &children, &n_children);
    if (children) {
        for (unsigned int i = 0; i < n_children; ++i) {
            XSetWindowBorderWidth(display, children[i], border_width);
            set_window_border(display, children[i], false);
            // Thay đổi vị trí của các cửa sổ có sẵn để không bị taskbar che
            XWindowAttributes child_attrs;
            XGetWindowAttributes(display, children[i], &child_attrs);
            if (child_attrs.y < statusbar_height) {
                XMoveWindow(display, children[i], child_attrs.x, child_attrs.y + statusbar_height);
            }
        }
        XFree(children);
    }

    // Grab các phím tắt
    KeyCode key_enter_keycode = XKeysymToKeycode(display, XK_Return);
    XGrabKey(display, key_enter_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);

    KeyCode key_d_keycode = XKeysymToKeycode(display, XK_d);
    XGrabKey(display, key_d_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);

    KeyCode key_e_keycode = XKeysymToKeycode(display, XK_e);
    XGrabKey(display, key_e_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    
    KeyCode key_q_keycode = XKeysymToKeycode(display, XK_q);
    XGrabKey(display, key_q_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(display, key_q_keycode, Mod4Mask | ShiftMask, root_window, True, GrabModeAsync, GrabModeAsync);
    
    KeyCode key_m_keycode = XKeysymToKeycode(display, XK_m);
    XGrabKey(display, key_m_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);

    std::cout << "Grabbed keybindings: Super + Enter (Terminal), Super + D (dmenu), Super + E (Dolphin), Super + Q (Close), Super + Shift + Q (Kill), Super + M (Exit WM)." << std::endl;

    XEvent event;

    while (true) {
        XNextEvent(display, &event);

        switch (event.type) {
            case CreateNotify:
                XSelectInput(display, event.xcreatewindow.window, StructureNotifyMask | ExposureMask | KeyPressMask | ButtonPressMask | EnterWindowMask);
                XSetWindowBorderWidth(display, event.xcreatewindow.window, border_width);
                set_window_border(display, event.xcreatewindow.window, false);
                break;

            case MapRequest:
                if (std::find(managed_windows.begin(), managed_windows.end(), event.xmaprequest.window) == managed_windows.end()) {
                    managed_windows.push_back(event.xmaprequest.window);
                }
                XMapWindow(display, event.xmaprequest.window);
                tile_windows(display, root_window);

                if (focused_window != None) {
                    set_window_border(display, focused_window, false);
                }
                XSetInputFocus(display, event.xmaprequest.window, RevertToPointerRoot, CurrentTime);
                set_window_border(display, event.xmaprequest.window, true);
                focused_window = event.xmaprequest.window;
                break;

            case ConfigureRequest:
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
                managed_windows.erase(std::remove(managed_windows.begin(), managed_windows.end(), event.xdestroywindow.window), managed_windows.end());
                if(focused_window == event.xdestroywindow.window) {
                    focused_window = None;
                }
                tile_windows(display, root_window);
                break;
            
            case PropertyNotify:
                if (event.xproperty.window == root_window && event.xproperty.atom == NET_WM_NAME) {
                    draw_statusbar(display);
                }
                break;
            
            case Expose:
                if (event.xexpose.window == statusbar_window) {
                    draw_statusbar(display);
                }
                break;

            case ButtonPress: {
                if (event.xbutton.button == 1) {
                    is_moving = true;
                    current_moving_window = event.xbutton.subwindow;
                    if (current_moving_window == None) {
                        is_moving = false;
                        break;
                    }
                    XWindowAttributes win_attrs;
                    XGetWindowAttributes(display, current_moving_window, &win_attrs);
                    start_win_x = win_attrs.x;
                    start_win_y = win_attrs.y;
                    start_x = event.xbutton.x_root;
                    start_y = event.xbutton.y_root;
                    XGrabPointer(display, root_window, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                                 GrabModeAsync, GrabModeAsync, root_window, None, CurrentTime);
                }
                break;
            }

            case MotionNotify: {
                if (is_moving && current_moving_window != None) {
                    int new_x = start_win_x + (event.xmotion.x_root - start_x);
                    int new_y = start_win_y + (event.xmotion.y_root - start_y);
                    XMoveWindow(display, current_moving_window, new_x, new_y);
                }
                break;
            }

            case ButtonRelease: {
                is_moving = false;
                current_moving_window = None;
                XUngrabPointer(display, CurrentTime);
                tile_windows(display, root_window);
                break;
            }

            case EnterNotify: {
                if (event.xcrossing.window != root_window && event.xcrossing.window != focused_window) {
                    if (focused_window != None) {
                        set_window_border(display, focused_window, false);
                    }
                    XSetInputFocus(display, event.xcrossing.window, RevertToPointerRoot, CurrentTime);
                    set_window_border(display, event.xcrossing.window, true);
                    focused_window = event.xcrossing.window;
                }
                break;
            }

            case KeyPress:
                if (event.xkey.keycode == key_enter_keycode && (event.xkey.state & Mod4Mask)) {
                    execute_command({"konsole"});
                } else if (event.xkey.keycode == key_d_keycode && (event.xkey.state & Mod4Mask)) {
                    execute_command({"dmenu_run"});
                } else if (event.xkey.keycode == key_e_keycode && (event.xkey.state & Mod4Mask)) {
                    execute_command({"dolphin"});
                } else if (event.xkey.keycode == key_q_keycode && (event.xkey.state & Mod4Mask) && !(event.xkey.state & ShiftMask)) {
                    if (focused_window != None && focused_window != root_window) {
                        close_window(display, focused_window);
                    }
                } else if (event.xkey.keycode == key_q_keycode && (event.xkey.state & Mod4Mask) && (event.xkey.state & ShiftMask)) {
                    if (focused_window != None && focused_window != root_window) {
                        XKillClient(display, focused_window);
                    }
                } else if (event.xkey.keycode == key_m_keycode && (event.xkey.state & Mod4Mask)) {
                    XCloseDisplay(display);
                    return 0;
                }
                break;
            
            default:
                break;
        }
    }

    XCloseDisplay(display);
    return 0;
}
