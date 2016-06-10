Graphene Desktop
==========

The desktop environment of VeltOS, an Arch Linux-based operating system
built entirely upon community input. Check out [velt.io](https://www.velt.io) more info.

Installing for Arch
--------

If you are running Arch Linux, you can install Graphene Desktop by adding
*vosrepo.velt.io/x86_64* to your list of repositories and installing the
package *graphene-desktop* via pacman. [Click here for more detail.](https://velt.io/veltos-installation/)

Building Manually
--------

graphene-desktop uses the following libraries and packages:

    - cmake (build only)
    - accountsservice
    - glib2
    - gnome-control-center
    - gnome-menus
    - gtk+-3.0 >= 3.20
    - libpulse
    - libwnck3
    - mutter >= 3.20
    - nautilus (optional)
    - paper-gtk-theme-git >= 297 (AUR or vosrepo.velt.io/x86_64)
    - paper-icon-theme-git >= 552 (AUR or vosrepo.velt.io/x86_64)

All of these are available from Arch's official repositories, the AUR,
or the vos repository. For any other Linux distros, you're on your own.

It will also be necessary to install xorg, as well as a way to start
it (xorg-xinit or a login manager such as LightDM). To run manually,
execute 'graphene-session' from a xorg terminal after installing.

Download/clone this repo and

```bash

    cd graphene-desktop
    cmake .
    sudo make all install
```

graphene-desktop's data files (including panel applets) are stored in /usr/share/graphene.
All files here are customizable, however know that **running "sudo make install"
will overwrite any custom changes made to that directory.**

Uninstall by running 'sudo make uninstall' in the graphene-desktop directory. This only works
when install_manifest.txt, a file created after installing, is available.

License
--------

Most of Graphene Desktop uses the Apache License 2.0 (ASL). Although we support Free Software,
the Velt team chose ASL because we want to give users the freedom to do anything they want
with our software.

However, not all of Graphene could be licensed under ASL. For example, the window manager and
all source files in the wm directory must be licensed under the GPLv3 because libmutter is also
GPLv3. The license information for each applicable file is listed at the top of the file.

Authors
--------

Aidan Shafran <zelbrium@gmail.com>

Thanks
--------

Graphene was heavily influenced and inspired by the amazing [Budgie Desktop](https://solus-project.com/budgie/), developed by Ikey Doherty <ikey@solus-project.com>.