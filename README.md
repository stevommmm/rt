# rt
hacky session terminal recording to syslog

Makes use of epoll + pty allocation for a child bash process with full handling of Control-C

100% not for production, I was only curious how hard it was to write a session recording shell.

Syslog ident uses `rt-ses-$USERNAME` and simply logs input and output

```
Dec 17 21:29:46 marvin rt-ses-c45y[21971]: rt session beginning
Dec 17 21:29:46 marvin rt-ses-c45y[21971]: bash: no job control in this shell
Dec 17 21:29:48 marvin rt-ses-c45y[21971]: c45y@marvin:~/Git/rt $ ls
Dec 17 21:29:48 marvin rt-ses-c45y[21971]: LICENSE  Makefile  README.md  rt  src
Dec 17 21:29:48 marvin rt-ses-c45y[21971]: c45y@marvin:~/Git/rt $ exit
```
