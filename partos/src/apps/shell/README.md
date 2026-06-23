# shell

This directory holds the first PartOS application: the shell.

The bootstrap loader now expects to find the shell image on disk as:

- `/SHELL.COM`

`SHELL.COM` wraps one relocatable `.XL` payload plus its COM header. The shell
uses only the public `"partos"` service and currently runs commands in the
foreground: it waits for the child process to exit before it prints the next
prompt.
