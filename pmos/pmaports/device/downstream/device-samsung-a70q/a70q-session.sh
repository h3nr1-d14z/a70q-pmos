# a70q session environment (sourced by the login shell that starts Sxmo).

# WebKitGTK (Epiphany) sandboxes web content with bubblewrap, and bubblewrap >= 0.11 needs
# openat2() (Linux 5.6); the 4.14 kernel returns ENOSYS ("bwrap: Can't open source /") and the
# browser aborts. Run WebKit without its bwrap sandbox instead (ERRORS C64).
export WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1

# Alpine's dunst is built with SYSCONFDIR=/usr/local/etc/xdg and only falls back to /etc/xdg when
# XDG_CONFIG_DIRS is set (our /etc/xdg/dunst/dunstrc). /etc/xdg is the XDG default anyway.
export XDG_CONFIG_DIRS="${XDG_CONFIG_DIRS:-/etc/xdg}"

# dark theme: libadwaita/GTK4 apps (Epiphany; web pages then get prefers-color-scheme: dark).
# GTK3 apps read /etc/gtk-3.0/settings.ini (gtk-application-prefer-dark-theme).
export ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}"
