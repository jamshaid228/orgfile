## Setup and Installation

Presently, this project has been tested on the following distributions:

* RHEL 7.0
* RHEL 7.3
* CentOS 7.6

The following compilers are supported:

* g++ 4.8

The MariaDB packages are required in order to build mysql2ssim and ssim2mysql tools.

    yum install -y mariadb mariadb-devel mariadb-server
    
All commands can be issued from this, top-level directory.
Just add the relative path bin/ to your path.

    set PATH=$PATH:bin/

To build everything, you can run make (provided for convenience)
or the bootstapped version of abt called ai:

    ai

This should build abt using a bootstrapped shell script, then switch to abt
and build the rest. If any of this fails, you may need to file a bug report.

### Platform Short List

Support for g++ 8.x, clang, other Linux distributions, FreeBSD, Windows (Cygwin) and Darwin
is very must desired, so if you'd like to help, please contact alexei@lebe.dev

### Editor configuration files

See files in conf/ for sample config files that provide ssim syntax highlighting, etc.
Here are some commands to get set it up.

    ln -s $PWD/conf/emacs.el ~/.emacs
    ln -s $PWD/conf/elisp ~/elisp
    ln -s $PWD/conf/vimrc ~/.vimrc
    ln -s $PWD/conf/vim ~/.vim
    ln -s $PWD/conf/alexei/bash_profile ~/.bash_profile

### Environment Variables

There are no environment variables that we either or rely on.
The ssimfiles offer plenty of room for structured configs.