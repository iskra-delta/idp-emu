# apps

This directory is reserved for user-space PartOS applications.

Applications live outside the kernel, driver and OS-layer source trees and are
meant to be built as separate loadable images such as `.xl` programs.

The first application subtree is:

- `partos/src/apps/shell/`

Build integration for applications is intentionally not wired yet; this commit
only establishes the source layout.
