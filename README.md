![build](https://github.com/linuxmint/xapp/actions/workflows/build.yml/badge.svg)

This project gathers the components which are common to multiple GTK desktop environments (Cinnamon, MATE and Xfce) and required to implement cross-DE solutions.

# libxapp

A library available in C, Python and other languages (via Gobject Introspection).

# API Reference

https://projects.linuxmint.com/xapp/reference/index.html

# xapp-common

A set of resources and tools.

# Scope

In the long term Xapps are there to provide applications to users in multiple DEs, but also to provide building blocks, resources and libraries to Linux developers who want to develop applications which work well in multiple DEs.

This project is still relatively new though and we can't yet commit to a stable ABI.

You are welcome to use libxapp or xapps-common in your application, but if you do, please make sure to tell us, so that we can communicate with you going forward about any potential changes.
