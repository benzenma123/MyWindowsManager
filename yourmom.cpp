#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>

// Các biến toàn cục
Atom WM_PROTOCOLS;
Atom WM_DELETE_WINDOW;
// Các biến để quản lý việc di chuyển cửa sổ
bool is_moving = false;
int start_x, start_y;
int start_win_x, start_win_y;
Window current_moving_window = None;

// Các biến để quản lý layout và cửa sổ
std::vector<Window> managed_windows;
const float master_ratio = 0.6; // Cửa sổ chính chiếm 60% màn hình
const int border_width = 2; // Chiều rộng viền cửa sổ

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

// Hàm tiling chính
void tile_windows(Display* display, Window root_window) {
    if (managed_windows.empty()) return;

    XWindowAttributes root_attrs;
    XGetWindowAttributes(display, root_window, &root_attrs);
    
    const int num_windows = managed_windows.size();
    const int screen_width = root_attrs.width;
    const int screen_height = root_attrs.height;
    
    // Nếu chỉ có một cửa sổ, nó chiếm toàn màn hình
    if (num_windows == 1) {
        XMoveResizeWindow(display, managed_windows[0], 0, 0, screen_width - 2*border_width, screen_height - 2*border_width);
        return;
    }
    
    // Cửa sổ chính chiếm phần bên trái
    const int master_width = screen_width * master_ratio;
    XMoveResizeWindow(display, managed_windows[0], 0, 0, master_width - 2*border_width, screen_height - 2*border_width);

    // Các cửa sổ phụ xếp chồng bên phải
    const int stack_width = screen_width - master_width;
    const int stack_height = screen_height / (num_windows - 1);
    
    for (int i = 1; i < num_windows; ++i) {
        XMoveResizeWindow(display, managed_windows[i], 
                          master_width, // Vị trí X
                          (i - 1) * stack_height, // Vị trí Y
                          stack_width - 2*border_width, // Chiều rộng
                          stack_height - 2*border_width); // Chiều cao
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

    XSelectInput(display, root_window, SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask | ButtonPressMask | EnterWindowMask);

    XSetWindowAttributes attributes;
    attributes.event_mask = SubstructureNotifyMask | SubstructureRedirectMask | KeyPressMask | ButtonPressMask | EnterWindowMask;
    attributes.border_pixel = BlackPixel(display, DefaultScreen(display));

    XGrabServer(display);
    if (!XChangeWindowAttributes(display, root_window, CWEventMask, &attributes)) {
        std::cerr << "Another Window Manager is already running! Cannot become primary WM." << std::endl;
        XUngrabServer(display);
        XCloseDisplay(display);
        return 1;
    }
    XUngrabServer(display);
    std::cout << "Became Window Manager (or attempted to)." << std::endl;
    
    // Đặt viền cho các cửa sổ hiện tại
    Window* children;
    unsigned int n_children;
    XQueryTree(display, root_window, &root_window, &root_window, &children, &n_children);
    if (children) {
        for (unsigned int i = 0; i < n_children; ++i) {
            XSetWindowBorderWidth(display, children[i], border_width);
            XSetWindowBorder(display, children[i], WhitePixel(display, DefaultScreen(display)));
        }
        XFree(children);
    }


    // Grab các phím tắt
    KeyCode key_enter_keycode = XKeysymToKeycode(display, XK_Return);
    XGrabKey(display, key_enter_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    std::cout << "Grabbed Super + Enter to open konsole." << std::endl;

    KeyCode key_a_keycode = XKeysymToKeycode(display, XK_a);
    XGrabKey(display, key_a_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    std::cout << "Grabbed Super + A to open wofi." << std::endl;

    KeyCode key_e_keycode = XKeysymToKeycode(display, XK_e);
    XGrabKey(display, key_e_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    std::cout << "Grabbed Super + E to open dolphin." << std::endl;
    
    KeyCode key_q_keycode = XKeysymToKeycode(display, XK_q);
    XGrabKey(display, key_q_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    std::cout << "Grabbed Super + Q to close window." << std::endl;

    KeyCode key_m_keycode = XKeysymToKeycode(display, XK_m);
    XGrabKey(display, key_m_keycode, Mod4Mask, root_window, True, GrabModeAsync, GrabModeAsync);
    std::cout << "Grabbed Super + M to exit WM." << std::endl;

    XEvent event;
    while (true) {
        XNextEvent(display, &event);

        switch (event.type) {
            case CreateNotify:
                std::cout << "CreateNotify event: New window created, ID: " << event.xcreatewindow.window << std::endl;
                XSelectInput(display, event.xcreatewindow.window, StructureNotifyMask | ExposureMask | KeyPressMask | ButtonPressMask | EnterWindowMask);
                XSetWindowBorderWidth(display, event.xcreatewindow.window, border_width);
                XSetWindowBorder(display, event.xcreatewindow.window, WhitePixel(display, DefaultScreen(display)));
                break;

            case MapRequest:
                std::cout << "MapRequest event: Application requests window display ID: " << event.xmaprequest.window << std::endl;
                managed_windows.push_back(event.xmaprequest.window);
                XMapWindow(display, event.xmaprequest.window);
                tile_windows(display, root_window);
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
                for (size_t i = 0; i < managed_windows.size(); ++i) {
                    if (managed_windows[i] == event.xdestroywindow.window) {
                        managed_windows.erase(managed_windows.begin() + i);
                        break;
                    }
                }
                tile_windows(display, root_window);
                break;

            case ButtonPress: {
                if (event.xbutton.button == 1) { // Chuột trái
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
                tile_windows(display, root_window); // Sắp xếp lại sau khi di chuyển xong
                break;
            }

            case EnterNotify: {
                if (event.xcrossing.mode == NotifyNormal) {
                    XSetInputFocus(display, event.xcrossing.window, RevertToPointerRoot, CurrentTime);
                    std::cout << "Focus set to window: " << event.xcrossing.window << std::endl;
                }
                break;
            }

            case KeyPress:
                if (event.xkey.keycode == key_enter_keycode && (event.xkey.state & Mod4Mask)) {
                    std::cout << "Super + Enter pressed! Opening konsole..." << std::endl;
                    execute_command({"konsole"});
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
                } else if (event.xkey.keycode == key_a_keycode && (event.xkey.state & Mod4Mask)) {
                    std::cout << "Super + A pressed! Opening wofi..." << std::endl;
                    execute_command({"wofi", "--show", "drun"});
                } else if (event.xkey.keycode == key_e_keycode && (event.xkey.state & Mod4Mask)) {
                    std::cout << "Super + E pressed! Opening dolphin..." << std::endl;
                    execute_command({"dolphin"});
                } else if (event.xkey.keycode == key_m_keycode && (event.xkey.state & Mod4Mask)) {
                    std::cout << "Super + M pressed! Exiting WM..." << std::endl;
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
