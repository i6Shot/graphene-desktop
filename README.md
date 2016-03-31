Graphene Desktop
==========

The desktop environment of VeltOS, an Arch Linux-based operating system
built entirely upon community input. Check out [velt.io](https://www.velt.io) more info.

Building for Arch
--------

If you are running Arch Linux, you can install Graphene Desktop by adding
*vosrepo.velt.io/x86_64* to your list of repositories and installing the
package *graphene-desktop* via pacman.

More detailed install instructions are available at [velt.io/vosinfo](https://velt.io/vosinfo/).

Building Manually
--------

graphene-desktop uses the following libraries:

    - accountsservice
    - glib2
    - gnome-control-center (optional)
    - gnome-menus
    - gobject-introspection
    - gtk3
    - libpeas
    - libpulse
    - libwnck3
    - mutter
    - nautilus (optional)
    - paper-gtk-theme-git (AUR or vosrepo.velt.io/x86_64)
    - paper-icon-theme-git (AUR or vosrepo.velt.io/x86_64)
    - python-cairo
    - python-gobject

All of these are available from Arch's official repositories, the AUR,
or the vos repository. For any other Linux distros, you're on your own.

It will also be necessary to install xorg, as well as a way to start
it (xorg-xinit or a login manager such as LightDM). To run manually,
execute 'graphene-session' from a xorg terminal after installing.

Download/clone this repo and

```bash

    cd graphene-desktop
    sudo make
    sudo make install
```

graphene-desktop's data files (including panel applets) are stored in /usr/share/graphene.
All files here are customizable, however know that **running "sudo make install"
will overwrite any custom changes made to that directory.**

Eventually, this single-file make will be replaced with Autotools. Once I can
figure out how to use Autotools.

Authors
--------

Aidan Shafran <zelbrium@gmail.com>

Thanks
--------

Graphene was heavily influenced and inspired by the amazing [Budgie Desktop](https://solus-project.com/budgie/), developed by Ikey Doherty <ikey@solus-project.com>.