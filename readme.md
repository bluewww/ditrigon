# Ditrigon
[![GNU/Linux](https://github.com/bluewww/ditrigon/actions/workflows/ubuntu-build.yml/badge.svg)](https://github.com/bluewww/ditrigon/actions/workflows/ubuntu-build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)

Ditrigon is an IRC client for GNU/Linux using GTK4 and libadwaita.
See [IRCHelp.org](http://irchelp.org) for information about IRC in general.  

Ditrigon started is life as a fork of HexChat which has been archived.


<img width="1721" height="1173" alt="ditrigon" src="https://github.com/user-attachments/assets/93ab11d7-71e1-486f-9b97-ce7732646003" />


# Installation

Use `meson` to build and install Ditrigon.

```bash
$ meson setup _builddir
$ meson compile -C _builddir
$ meson install -C _builddir
```

# Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

# License
This program is released under the GPL v2 with the additional exemption
that compiling, linking, and/or using OpenSSL is allowed. You may
provide binary packages linked to the OpenSSL libraries, provided that
all other requirements of the GPL are met.
See file COPYING for details.

---

<sub>
X-Chat ("xchat") Copyright (c) 1998-2010 By Peter Zelezny.  
HexChat ("hexchat") Copyright (c) 2009-2014 By Berke Viktor.
Ditrigon ("ditrigon") Copyright (c) 2026 By bluewww.
</sub>
