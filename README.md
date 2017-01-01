Graphene Desktop
==========

The desktop environment of VeltOS, an Arch Linux-based operating system
built entirely upon community input. Check out [velt.io](https://www.velt.io)
for more info.

Screenshot
--------

![Screenshot of VeltOS](/screenshot.png?raw=true)
To continue the style of Mutter: a boring desktop wallpaper for the adult in you.

Installing for Arch
--------

If you are running Arch Linux, you can install Graphene Desktop by adding
*cdn.velt.io/vosrepo/$arch* to your list of repositories and installing the
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
    - mutter >= 3.22
    - nautilus (optional)
    - paper-gtk-theme-git >= 297 (AUR or cdn.velt.io/vosrepo/$arch)
    - paper-icon-theme-git >= 552 (AUR or cdn.velt.io/vosrepo/$arch)

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

graphene-desktop's data files are stored in /usr/share/graphene.
All files here are customizable, however know that **running "sudo make install"
will overwrite any custom changes made to that directory.**

Uninstall by running 'sudo make uninstall' in the graphene-desktop directory.
This only works when install_manifest.txt, a file created after installing, is
available.

License
--------

Currently, as a whole, the Panel is under the Apache License 2.0, and the
Window/Session Manager is under the GPLv3. However, different source files may
be under different licenses depending on how they're being used and which
libraries they require. *The license information for each file is listed at the
top of the file.*

Authors
--------

Aidan Shafran <zelbrium@gmail.com>  
Conner Novicki <connernovicki@gmail.com>

Thanks
--------

Graphene was heavily influenced and inspired by the amazing [Budgie Desktop](https://solus-project.com/budgie/), developed by Ikey Doherty <ikey@solus-project.com>.
