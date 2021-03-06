#include "x.hpp"

slop::XEngine* xengine = new slop::XEngine();

int slop::XEngineErrorHandler( Display* dpy, XErrorEvent* event ) {
    // Ignore XGrabKeyboard BadAccess errors, we can work without it.
    // 31 = XGrabKeyboard's request code
    if ( event->request_code == 31 && event->error_code == BadAccess ) {
        fprintf( stderr, "_X Error \"BadAccess\" for XGrabKeyboard ignored...\n" );
        return 0;
    }
    // Everything else should be fatal as I don't like undefined behavior.
    char buffer[1024];
    XGetErrorText( dpy, event->error_code, buffer, 1024 );
    fprintf( stderr,
             "_X Error of failed request:  %s\n_  Major opcode of failed request: % 3d\n_  Serial number of failed request:% 5li\n_  Current serial number in output stream:?????\n",
             buffer,
             event->request_code,
             event->serial );
    exit(1);
}

slop::XEngine::XEngine() {
    m_display = NULL;
    m_visual = NULL;
    m_screen = NULL;
    m_good = false;
    m_mousex = -1;
    m_mousey = -1;
    m_hoverWindow = None;
}

slop::XEngine::~XEngine() {
    if ( !m_good ) {
        return;
    }
    for ( unsigned int i=0; i<m_cursors.size(); i++ ) {
        if ( m_cursors.at( i ) ) {
            XFreeCursor( m_display, m_cursors[i] );
        }
    }
    XCloseDisplay( m_display );
}

bool slop::XEngine::mouseDown( unsigned int button ) {
    if ( button >= m_mouse.size() ) {
        return false;
    }
    return m_mouse.at( button );
}

int slop::XEngine::init( std::string display ) {
    // Initialize display
    m_display = XOpenDisplay( display.c_str() );
    if ( !m_display ) {
        fprintf( stderr, "Error: Failed to open X display %s\n", display.c_str() );
        return 1;
    }
    m_screen    = ScreenOfDisplay( m_display, DefaultScreen( m_display ) );
    m_visual    = DefaultVisual  ( m_display, XScreenNumberOfScreen( m_screen ) );
    m_colormap  = DefaultColormap( m_display, XScreenNumberOfScreen( m_screen ) );
    //m_root      = RootWindow     ( m_display, XScreenNumberOfScreen( m_screen ) );
    m_root      = DefaultRootWindow( m_display );

    m_good = true;
    XSetErrorHandler( slop::XEngineErrorHandler );
    selectAllInputs( m_root, EnterWindowMask );
    return 0;
}

bool slop::XEngine::anyKeyPressed() {
    if ( !m_good ) {
        return false;
    }
    // Thanks to SFML for some reliable key state grabbing.
    // Get the whole keyboard state
    char keys[ 32 ];
    XQueryKeymap( m_display, keys );
    // Each bit indicates a different key, 1 for pressed, 0 otherwise.
    // Every bit should be 0 if nothing is pressed.
    for ( unsigned int i = 0; i < 32; i++ ) {
        if ( keys[ i ] != 0 ) {
            return true;
        }
    }
    return false;
}

int slop::XEngine::grabKeyboard() {
    if ( !m_good ) {
        return 1;
    }
    int err = XGrabKeyboard( m_display, m_root, False, GrabModeAsync, GrabModeAsync, CurrentTime );
    if ( err != GrabSuccess ) {
        fprintf( stderr, "Warning: Failed to grab X keyboard.\n" );
        fprintf( stderr, "         This happens when something's already grabbed your keybaord.\n" );
        fprintf( stderr, "         slop should still run properly though.\n" );
        return 1;
    }
    return 0;
}

int slop::XEngine::releaseKeyboard() {
    if ( !m_good ) {
        return 1;
    }
    XUngrabKeyboard( m_display, CurrentTime );
    return 0;
}

void slop::XEngine::selectAllInputs( Window win, long event_mask) {
    Window root, parent;
    Window* children;
    unsigned int nchildren;
    XQueryTree( m_display, win, &root, &parent, &children, &nchildren );
    for ( unsigned int i=0;i<nchildren;i++ ) {
        XSelectInput( m_display, children[ i ], event_mask );
        selectAllInputs( children[ i ], event_mask );
    }
}

// Grabs the cursor, be wary that setCursor changes the mouse masks.
int slop::XEngine::grabCursor( slop::CursorType type ) {
    if ( !m_good ) {
        return 1;
    }
    int xfontcursor = getCursor( type );
    int err = XGrabPointer( m_display, m_root, True,
                            PointerMotionMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask,
                            GrabModeAsync, GrabModeAsync, None, xfontcursor, CurrentTime );
    if ( err != GrabSuccess ) {
        fprintf( stderr, "Error: Failed to grab X cursor.\n" );
        fprintf( stderr, "       This can be caused by launching slop weirdly.\n" );
        return 1;
    }
    // Quickly set the mouse position so we don't have to worry about x11 generating an event.
    Window root, child;
    int mx, my;
    int wx, wy;
    unsigned int mask;
    XQueryPointer( m_display, m_root, &root, &child, &mx, &my, &wx, &wy, &mask );
    m_mousex = mx;
    m_mousey = my;

    // Get the deepest available window.
    Window test = child;
    while( test ) {
        child = test;
        XQueryPointer( m_display, child, &root, &test, &mx, &my, &wx, &wy, &mask );
    }
    m_hoverWindow = child;
    return 0;
}

int slop::XEngine::releaseCursor() {
    if ( !m_good ) {
        return 1;
    }
    XUngrabPointer( m_display, CurrentTime );
    return 0;
}

void slop::XEngine::tick() {
    if ( !m_good ) {
        return;
    }
    XFlush( m_display );
    XEvent event;
    while ( XPending( m_display ) ) {
        XNextEvent( m_display, &event );
        switch ( event.type ) {
            case MotionNotify: {
                m_mousex = event.xmotion.x;
                m_mousey = event.xmotion.y;
                break;
            }
            case ButtonPress: {
                // Our pitiful mouse manager--
                if ( m_mouse.size() > event.xbutton.button ) {
                    m_mouse.at( event.xbutton.button ) = true;
                } else {
                    m_mouse.resize( event.xbutton.button+2, false );
                    m_mouse.at( event.xbutton.button ) = true;
                }
                break;
            }
            case EnterNotify: {
                if ( event.xcrossing.subwindow != None ) {
                    m_hoverWindow = event.xcrossing.subwindow;
                } else {
                    m_hoverWindow = event.xcrossing.window;
                }
                break;
            }
            case LeaveNotify: {
                break;
            }
            case ButtonRelease: {
                if ( m_mouse.size() > event.xbutton.button ) {
                    m_mouse.at( event.xbutton.button ) = false;
                } else {
                    m_mouse.resize( event.xbutton.button+2, false );
                    m_mouse.at( event.xbutton.button ) = false;
                }
                break;
            }
            // Due to X11 really hating applications grabbing the keyboard, we use XQueryKeymap to check for downed keys elsewhere.
            case KeyPress: {
                break;
            }
            case KeyRelease: {
                break;
            }
            default: break;
        }
    }
}

// This converts an enum into a preallocated cursor, the cursor will automatically deallocate itself on ~XEngine
Cursor slop::XEngine::getCursor( slop::CursorType type ) {
    int xfontcursor;
    switch ( type ) {
        default:
        case Left:                  xfontcursor = XC_left_ptr; break;
        case Crosshair:             xfontcursor = XC_crosshair; break;
        case Cross:                 xfontcursor = XC_cross; break;
        case UpperLeftCorner:       xfontcursor = XC_ul_angle; break;
        case UpperRightCorner:      xfontcursor = XC_ur_angle; break;
        case LowerLeftCorner:       xfontcursor = XC_ll_angle; break;
        case LowerRightCorner:      xfontcursor = XC_lr_angle; break;
    }
    Cursor newcursor = 0;
    if ( m_cursors.size() > xfontcursor ) {
        newcursor = m_cursors.at( xfontcursor );
    }
    if ( !newcursor ) {
        newcursor = XCreateFontCursor( m_display, xfontcursor );
        m_cursors.resize( xfontcursor+2, 0 );
        m_cursors.at( xfontcursor ) = newcursor;
    }
    return newcursor;
}

// Swaps out the current cursor, bewary that XChangeActivePointerGrab also resets masks, so if you change the mouse masks on grab you need to change them here too.
void slop::XEngine::setCursor( slop::CursorType type ) {
    if ( !m_good ) {
        return;
    }
    Cursor xfontcursor = getCursor( type );
    XChangeActivePointerGrab( m_display,
                              PointerMotionMask | ButtonPressMask | ButtonReleaseMask,
                              xfontcursor, CurrentTime );
}

void slop::WindowRectangle::setGeometry( Window win, bool decorations ) {
    if ( decorations ) {
        Window root, parent, test, junk;
        Window* childlist;
        unsigned int ujunk;
        unsigned int depth;
        // Try to find the actual decorations.
        test = win;
        int status = XQueryTree( xengine->m_display, test, &root, &parent, &childlist, &ujunk);
        while( parent != root ) {
            if ( !parent || !status ) {
                break;
            }
            test = parent;
            status = XQueryTree( xengine->m_display, test, &root, &parent, &childlist, &ujunk);
        }
        // Once found, proceed normally.
        if ( test && parent == root && status ) {
            XWindowAttributes attr;
            XGetWindowAttributes( xengine->m_display, test, &attr );
            m_width = attr.width;
            m_height = attr.height;
            m_border = attr.border_width;
            XTranslateCoordinates( xengine->m_display, test, attr.root, -attr.border_width, -attr.border_width, &(m_x), &(m_y), &junk );
            // We make sure we include borders, since we want decorations.
            m_width += m_border * 2;
            m_height += m_border * 2;
        }
        return;
    }
    Window junk;
    // Now here we should be able to just use whatever we get.
    XWindowAttributes attr;
    // We use XGetWindowAttributes to know our root window.
    XGetWindowAttributes( xengine->m_display, win, &attr );
    //m_x = attr.x;
    //m_y = attr.y;
    m_width = attr.width;
    m_height = attr.height;
    m_border = attr.border_width;
    XTranslateCoordinates( xengine->m_display, win, attr.root, -attr.border_width, -attr.border_width, &(m_x), &(m_y), &junk );
}
