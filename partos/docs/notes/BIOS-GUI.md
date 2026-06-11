# Note: BIOS Startup Screen Mockup

Category: BIOS / User Interface
Date(s): 2024-03-19, 2026-06-11 (merged from `2024-03-19_bios-gui`)

Mockup of the BIOS startup/summary screen, with Unix-style device naming.

~~~
Wecome to Iskra Delta Partner Model WF BIOS

        S E R I A L   P O R T S

                      IFACE     ALIAS     SPEED  B  P  S
        /DEV/TTYS0    KEYBOARD  /DEV/KBD    300  8  N  1
        /DEV/TTYS1    SCREEN    /DEV/CRT   9600  8  N  1
        /DEV/TTYS2    MOUSE     /DEV/MOUSE 2400  8  N  1
        /DEV/TTYS3    -

        P A R A L L E L   P O R T S

                     IFACE     ALIAS
        /DEV/LP0     printer   /DEV/PRINTER
        /DEV/LP1          -

        R E A L   T I M E   C L O C K

                     DATE         TIME
        /DEV/RTC     05/10/2023   18:06:22

        H A R D   D I S K S

                  TYPE    SIZE   CYLS  HEAD  PRECOMP  LANDZ  SECTOR
        /DEV/SDA  SEAGATE 10 MB  519   8
        /DEV/SDB  WD      40 MB

        F L O P P Y   D R I V E S

                 TYPE  SIZE    SIDE TRACK SEC/TRK SEC.SIZE
        /DEV/FD0 3.5"  1.440kB    2
        /DEV/FD1 5.25" 360kB      1

Press F1 for information about the BIOS.
~~~

## 2026-06-11 context

A screen like this will not fit the 2 KB ROM budget alongside drivers; it
belongs to the "BIOS loaded from disk" variant or to an OS-level utility.
Kept as the target look & feel and for the device-naming scheme
(/DEV/TTYSn, /DEV/FDn, /DEV/SDn, /DEV/RTC, /DEV/LPn), which the driver
format may want to adopt.
