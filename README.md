WHAT IS POWAUR?
---------------

powaur is an AUR helper written in C. Its main objective is to provide
a yaourt / pacman esque interface to the AUR.

Currently, powaur supports:

- Installing packages from the AUR with dependency resolution (v0.1.4)
- Checking for outdated AUR packages and updating them (v0.1.4)
- Showing a valid topological order for packages (v0.1.4)
- Multithreaded downloading of packages from the AUR with dependency resolution
- Searching for packages on the AUR
- Querying local and sync databases for package information
- Backing up your pacman local database
- Colorized output (new in v0.1.3)

Unlike full featured AUR helpers such as clyde, powaur is not meant to
replace pacman.


OBJECTIVES
----------

powaur provides an interface which is as close to that of yaourt and pacman
as possible, without sacrificing too much performance. After all, why learn
an additional set of commands when you are already familiar with one that
works? =)

powaur seeks to:

- provide an interface as similar to pacman as possible
- do things efficiently. Hence C is the language of choice


Inspiration for powaur has come from:

- cower
- pacman
- yaourt
- git


A NOTE ABOUT THE AUR
--------------------

The Arch User Repository (AUR) is a community driven repository. As such,
AUR packages are unsupported and come with a certain level of security risk.

As such, use of the `-S` flag to install AUR packages is highly discouraged.
You are highly advised to look through the PKGBUILDS before invoking makepkg
to build the packages.


CREDITS
------

Dave Reisner for allowing me to use code from cower (json parsing using yajl)

Some source code in powaur comes from the following projects:

- pacman
- git


BUG REPORTS
-----------

Please email bug reports to pangyanhan@gmail.com .


AUTHOR
------

Pang Yan Han <pangyanhan@gmail.com>
